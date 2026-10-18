#include "Client.h"
#include <cmath>
#include <cstring>

namespace RrfModules::CartographerV3 {
void Client::Start(const uint8_t requestedUuid[6], uint32_t hostRate, uint64_t time) noexcept {
    Stop();
    hostHz = hostRate; now = startedAt = time;
    std::memcpy(uuid, requestedUuid, sizeof(uuid));
    state = State::Identifying; fault = Fault::None; profile = nullptr;
    sequence = retryCount = queryRetries = 0;
    samples = invalidSamples = overwritten = retries = timeouts = unknownMessages = 0;
    consecutiveValid = 0; historyWrite = 0; lastSample = Sample(); lastReceivedSample = lastDataMcu = lastValidReceived = 0;
    everReady = false;
    baseCount = 0; baseAdc = 0; identifyOffset = 0;
    if (ProfileCount == 0 || ProfileCount >= 32) { Fail(Fault::UnknownProfile); return; }
    candidates = (1u << ProfileCount) - 1;
    parser.Reset(); clock.Reset(hostHz, 48000000);
    // Explicit UUID on a dedicated bus. Idempotent for an already assigned target.
    uint8_t assign[8] = {1}; std::memcpy(assign + 1, uuid, 6); assign[7] = NodeId;
    if (!transport.Send(0x3f0, assign, sizeof(assign))) { Fail(Fault::Transport); return; }
    IdentifyNext();
}
void Client::Stop() noexcept {
    state = State::Disabled; fault = Fault::None;
    pending = waitingResponse = streamEnabled = stopRequested = stopSent = false; txSize = txOffset = 0;
    lastSample.valid = false;
}
void Client::RequestStop(uint64_t time) noexcept {
    lastSample.valid = false;
    if (state == State::Disabled || state == State::Fault || profile == nullptr) { Stop(); return; }
    stopRequested = true; stopSent = false; stopAt = time;
    state = State::Stopping;
}
void Client::Fail(Fault reason) noexcept {
    if (state == State::Fault) return; // retain the first cause until an explicit stop/start
    state = State::Fault; fault = reason;
    pending = waitingResponse = false; txSize = txOffset = 0;
    lastSample.valid = false;
}
void Client::Request(uint8_t command, const uint32_t* args, size_t count, bool response) noexcept {
    payloadSize = EncodeUInt(command, payload);
    for (size_t i = 0; i < count; ++i) payloadSize += EncodeUInt(args[i], payload + payloadSize);
    txSize = MakeBlock(sequence, payload, payloadSize, tx); txOffset = 0;
    pending = true; waitingResponse = response; requestAt = now;
    retryCount = queryRetries = 0;
    Pump();
}
void Client::IdentifyNext() noexcept {
    const uint32_t args[] = {static_cast<uint32_t>(identifyOffset), 40};
    Request(1, args, 2, true);
}
void Client::Pump() noexcept {
    while (pending && txOffset < txSize) {
        const size_t chunk = txSize - txOffset > 8 ? 8 : txSize - txOffset;
        if (!transport.Send(CommandCanId, tx + txOffset, chunk)) return;
        if (txOffset == 0) sentAt = now;
        txOffset += chunk;
        if (txOffset == txSize) completedAt = now;
    }
}
void Client::Tick(uint64_t time) noexcept {
    now = time;
    if (state == State::Disabled || state == State::Fault) return;
    if (stopRequested) {
        if (now - stopAt > hostHz / 2) { Stop(); return; }
        if (!pending) {
            waitingResponse = false; // a read-only query response is dispensable while stopping
            if (stopSent) { Stop(); return; }
            const uint32_t disable = 0;
            Request(profile->stream, &disable, 1, false); stopSent = true;
        }
    } else {
        if (streamEnabled && lastReceivedSample != 0 && now - lastReceivedSample > hostHz / 10) { Fail(Fault::Stale); return; }
        if (streamEnabled && lastValidReceived != 0 && now - lastValidReceived > hostHz / 10) { Fail(Fault::Sensor); return; }
        if (streamEnabled && !everReady && now - startedAt > 5u * hostHz && !HasFreshSample(now)) { Fail(Fault::Sensor); return; }
        if (everReady && !clock.Ready(now)) { Fail(Fault::Stale); return; }
    }
    if (pending) {
        Pump();
        const uint64_t from = txOffset < txSize ? requestAt : completedAt;
        if (now - from > hostHz / 10) {
            if (++retryCount > 4) { ++timeouts; Fail(Fault::Timeout); return; }
            ++retries; txOffset = 0; requestAt = now; Pump();
        }
        return;
    }
    if (waitingResponse) {
        if (now - requestAt > hostHz / 2) {
            if (++queryRetries > 3) { ++timeouts; Fail(Fault::Timeout); return; }
            ++retries; requestAt = now; retryCount = 0;
            txSize = MakeBlock(sequence, payload, payloadSize, tx); txOffset = 0; pending = true; Pump();
        }
        return;
    }
    if (state != State::Streaming && state != State::Ready) return;
    if (!streamEnabled) {
        const uint32_t enable = 1;
        Request(profile->stream, &enable, 1, false);
        streamEnabled = true; startedAt = now;
        return;
    }
    if (now >= nextClock) {
        nextClock = now + hostHz / 5;
        Request(profile->getUptime, nullptr, 0, true);
    }
}
void Client::Receive(const uint8_t* data, size_t length, uint64_t received, uint64_t servicedAt) noexcept {
    if (state == State::Disabled || state == State::Fault) return;
    now = servicedAt;
    parser.Feed(data, length, received, OnBlock, this);
}
void Client::OnBlock(void* context, const uint8_t* block, size_t length, uint64_t received) noexcept {
    static_cast<Client*>(context)->HandleBlock(block, length, received);
}
void Client::HandleBlock(const uint8_t* block, size_t length, uint64_t received) noexcept {
    if (state == State::Disabled || state == State::Fault) return;
    const uint8_t ack = block[1] & 0x0f;
    if (pending && txOffset == txSize && ack == ((sequence + 1) & 0x0f)) {
        pending = false; sequence = ack;
    } else if (pending && length == 5 && state == State::Identifying && identifyOffset == 0 && ack != sequence) {
        // A new reader may attach to the same UUID after its earlier diagnostic session.
        sequence = ack; txSize = MakeBlock(sequence, payload, payloadSize, tx); txOffset = 0;
        if (++retryCount > 4) { Fail(Fault::Protocol); return; }
        ++retries;
    }
    size_t position = 2;
    while (position < length - 3) {
        uint32_t id;
        if (!DecodeUInt(block, length - 3, position, id)) { Fail(Fault::Protocol); return; }
        ResponseFormat format = {0, 2, 2}; // identify: offset, buffer
        if (profile == nullptr) {
            if (id != 0) return; // unsolicited data cannot be interpreted before identify validation
        } else {
            bool found = false;
            for (size_t i = 0; i < profile->responseCount; ++i) {
                if (profile->responses[i].id == id) { format = profile->responses[i]; found = true; break; }
            }
            if (!found) { ++unknownMessages; Fail(Fault::Protocol); return; }
        }
        if (format.count > 8) { Fail(Fault::Protocol); return; }
        uint32_t args[8] = {};
        const uint8_t* buffers[8] = {};
        for (unsigned i = 0; i < format.count; ++i) {
            if ((format.buffers & (1u << i)) != 0) {
                if (position >= length - 3) { Fail(Fault::Protocol); return; }
                args[i] = block[position++];
                if (args[i] > length - 3 - position) { Fail(Fault::Protocol); return; }
                buffers[i] = block + position; position += args[i];
            } else if (!DecodeUInt(block, length - 3, position, args[i])) { Fail(Fault::Protocol); return; }
        }
        const bool queryResponse = !pending && waitingResponse && ack == sequence;
        HandleMessage(id, args, buffers, received, queryResponse);
        if (state == State::Fault) return;
    }
}
void Client::HandleMessage(uint32_t id, const uint32_t* args, const uint8_t* const* buffers, uint64_t received, bool queryResponse) noexcept {
    if (state == State::Identifying && id == 0 && queryResponse) {
        if (args[0] < identifyOffset) return; // harmless duplicate response
        if (args[0] != identifyOffset || args[1] > 40 || (args[1] != 0 && buffers[1] == nullptr)) { Fail(Fault::Protocol); return; }
        for (size_t i = 0; i < ProfileCount; ++i) {
            if ((candidates & (1u << i)) == 0) continue;
            const Profile& candidate = Profiles[i];
            if (candidate.dictionary == nullptr || identifyOffset + args[1] > candidate.dictionarySize ||
                (args[1] != 0 && std::memcmp(candidate.dictionary + identifyOffset, buffers[1], args[1]) != 0) ||
                (args[1] == 0 && identifyOffset != candidate.dictionarySize)) candidates &= ~(1u << i);
        }
        if (candidates == 0) { Fail(Fault::UnknownProfile); return; }
        pending = waitingResponse = false;
        identifyOffset += args[1];
        if (args[1] != 0) { IdentifyNext(); return; }
        for (size_t i = 0; i < ProfileCount; ++i) if (candidates & (1u << i)) { profile = &Profiles[i]; break; }
        if (profile == nullptr) { Fail(Fault::UnknownProfile); return; }
        clock.Reset(hostHz, profile->clockHz);
        state = State::Identity;
        Request(profile->getId, nullptr, 0, true);
        return;
    }
    if (profile == nullptr) return;
    if (id == profile->startingResponse || id == profile->shutdownResponse || id == profile->isShutdownResponse) {
        Fail(id == profile->startingResponse ? Fault::Reboot : Fault::Shutdown); return;
    }
    if (state == State::Identity && id == profile->idResponse && queryResponse) {
        if (args[0] != 6 || buffers[0] == nullptr || args[1] != NodeId || std::memcmp(buffers[0], uuid, 6) != 0) { Fail(Fault::Identity); return; }
        pending = waitingResponse = false; state = State::Configuration;
        Request(profile->getConfig, nullptr, 0, true);
    } else if (state == State::Configuration && id == profile->configResponse && queryResponse) {
        if (args[2] != 0) { Fail(Fault::Shutdown); return; }
        pending = waitingResponse = false; state = State::Base;
        const uint32_t baseArgs[] = {6, 0}; Request(profile->baseRead, baseArgs, 2, true);
    } else if (state == State::Base && id == profile->baseResponse && queryResponse) {
        if (args[0] != 6 || buffers[0] == nullptr || args[1] != 0) { Fail(Fault::Protocol); return; }
        const uint8_t* bytes = buffers[0];
        baseCount = static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
                    (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
        baseAdc = static_cast<uint16_t>(bytes[4] | (bytes[5] << 8));
        if (baseCount == 0 || baseCount >= 0x0fffffffu || baseAdc == 0 || baseAdc >= profile->adcMax * profile->smoothCount) { Fail(Fault::Sensor); return; }
        pending = waitingResponse = false; state = State::Streaming; streamEnabled = false; nextClock = now;
    } else if ((state == State::Streaming || state == State::Ready) && id == profile->uptimeResponse && queryResponse && payload[0] == profile->getUptime) {
        if (retryCount == 0 && queryRetries == 0) clock.Observe((static_cast<uint64_t>(args[0]) << 32) | args[1], sentAt, received);
        pending = waitingResponse = false;
        if (clock.Rebooted()) Fail(Fault::Reboot);
    } else if ((state == State::Streaming || state == State::Ready) && id == profile->dataResponse) Data(args, received);
}
void Client::Data(const uint32_t* args, uint64_t received) noexcept {
    Sample sample;
    sample.count = args[1]; sample.adcSum = args[2];
    lastReceivedSample = received;
    sample.valid = sample.count != 0 && sample.count != 0x05000100 && sample.count != 0x0fffffff &&
        sample.count <= (static_cast<uint64_t>(baseCount) * 135) / 100 &&
        sample.adcSum != 0 && sample.adcSum < profile->adcMax * profile->smoothCount;
    sample.frequency = sample.count * (static_cast<double>(profile->clockHz) / 2.0 / 268435456.0);
    if (sample.valid) {
        const double adc = static_cast<double>(sample.adcSum) / (profile->adcMax * profile->smoothCount);
        const double resistance = 10000.0 * adc / (1.0 - adc);
        sample.temperature = 1.0 / (1.0 / 298.15 + std::log(resistance / 47000.0) / 4041.0) - 273.15;
        sample.valid = std::isfinite(sample.temperature) && sample.temperature >= -50 && sample.temperature <= 150;
    }
    sample.valid = sample.valid && clock.Convert(args[0], sample.mcuClock, sample.hostTime);
    if (sample.valid) {
        const uint64_t allowance = static_cast<uint64_t>(clock.UncertaintyUs(received) + 2000) * hostHz / 1000000;
        sample.valid = sample.hostTime <= received + allowance &&
            sample.hostTime + hostHz / 50 >= received &&
            (lastDataMcu == 0 || sample.mcuClock > lastDataMcu);
    }
    if (sample.valid) { lastDataMcu = sample.mcuClock; lastValidReceived = received; if (consecutiveValid < 3) ++consecutiveValid; }
    else { ++invalidSamples; consecutiveValid = 0; }
    ++samples;
    if (samples > 64) ++overwritten;
    history[historyWrite] = sample; historyWrite = (historyWrite + 1) % 64;
    lastSample = sample;
    state = sample.valid && consecutiveValid >= 3 && clock.Ready(received) && clock.UncertaintyUs(received) <= 5000 ? State::Ready : State::Streaming;
    everReady = everReady || state == State::Ready;
}
uint32_t Client::SampleAgeUs(uint64_t time) const noexcept {
    if (samples == 0 || !lastSample.valid) return UINT32_MAX;
    const uint64_t age = (time > lastSample.hostTime ? time - lastSample.hostTime : 0) * 1000000 / hostHz;
    return age > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(age);
}
bool Client::HasFreshSample(uint64_t time) const noexcept {
    return state == State::Ready && lastSample.valid && clock.Ready(time) && clock.UncertaintyUs(time) <= 5000 && SampleAgeUs(time) < 20000;
}
bool Client::RecentSample(size_t age, Sample& sample) const noexcept {
    if (age >= 64 || age >= samples) return false;
    sample = history[(historyWrite + 63 - age) % 64];
    return true;
}
const char* Client::StateName(State value) noexcept {
    static const char* const names[] = {"disabled", "identifying", "identity", "configuration", "base", "streaming", "ready", "stopping", "fault"};
    return names[static_cast<unsigned>(value)];
}
const char* Client::FaultName(Fault value) noexcept {
    static const char* const names[] = {"none", "transport", "timeout", "unknown_profile", "protocol", "identity", "shutdown", "reboot", "stale", "sensor"};
    return names[static_cast<unsigned>(value)];
}
}
