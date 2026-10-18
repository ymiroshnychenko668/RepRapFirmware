#include "CartographerV3.h"
#include "CanTransport.h"
#include "Messages.h"
#include <GCodes/GCodeBuffer/GCodeBuffer.h>
#include <Hardware/IoPorts.h>
#include <Movement/StepTimer.h>
#include <Platform/Platform.h>
#include <Platform/RepRap.h>
#include <cstring>
#include <cinttypes>

namespace RrfModules::CartographerV3 {
namespace {
CanTransport can;
Client client(can);
IoPort receivePin, transmitPin;
bool enabled = false, connecting = false, ukrainian = false;
uint8_t selectedUuid[6] = {};
uint32_t previousClock = 0;
uint64_t extendedClock = 0;
uint64_t Now() noexcept {
    const uint32_t ticks = StepTimer::GetTimerTicks();
    extendedClock += static_cast<uint32_t>(ticks - previousClock); previousClock = ticks;
    return extendedClock;
}
void Release() noexcept { can.Stop(); receivePin.Release(); transmitPin.Release(); enabled = connecting = false; }
void Status(const StringRef& reply, bool detailed = true) noexcept {
    const uint64_t now = Now();
    const Sample& sample = client.LastSample();
    reply.printf("cartographer-v3 state=%s fault=%s fresh=%u uuid=%02x%02x%02x%02x%02x%02x profile=\"%s\"",
        connecting ? "can_starting" : Client::StateName(client.GetState()), Client::FaultName(client.GetFault()),
        client.HasFreshSample(now) ? 1u : 0u, selectedUuid[0], selectedUuid[1], selectedUuid[2], selectedUuid[3], selectedUuid[4], selectedUuid[5],
        client.GetProfile() != nullptr ? client.GetProfile()->name : "none");
    if (!detailed) return;
    reply.catf("\nraw=%" PRIu32 " adc_sum=%" PRIu32 " frequency=%.3f temperature=%.3f valid=%u age_us=%" PRIu32,
        sample.count, sample.adcSum, static_cast<double>(sample.frequency), static_cast<double>(sample.temperature),
        sample.valid ? 1u : 0u, client.SampleAgeUs(now));
    reply.catf("\nsamples=%" PRIu32 " invalid=%" PRIu32 " overwritten=%" PRIu32 " retries=%" PRIu32 " timeouts=%" PRIu32 " crc_errors=%" PRIu32 " malformed=%" PRIu32 " unknown_messages=%" PRIu32,
        client.samples, client.invalidSamples, client.overwritten, client.retries, client.timeouts, client.BadCrc(), client.Malformed(), client.unknownMessages);
    reply.catf("\nclock_samples=%" PRIu32 " clock_ready=%u rtt_us=%" PRIu32 " uncertainty_us=%" PRIu32 " rx_frames=%" PRIu32 " rx_overflow=%" PRIu32 " can_esr=%08" PRIx32,
        client.Clock().Count(), client.Clock().Ready(now) ? 1u : 0u, client.Clock().RttUs(), client.Clock().UncertaintyUs(now),
        can.RxCount(), can.Overflow(), can.Errors());
    reply.catf("\nsample_clock=%" PRIu64 " sample_host_ticks=%" PRIu64 " host_rate=%" PRIu32 " base_count=%" PRIu32 " base_adc=%u",
        sample.mcuClock, sample.hostTime, StepTimer::GetTickRate(), client.BaseCount(), client.BaseAdc());
}
int Hex(char value) noexcept {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}
}
void Init() noexcept { client.Stop(); Release(); previousClock = StepTimer::GetTimerTicks(); extendedClock = previousClock; }
void Spin() noexcept {
    const uint64_t now = Now();
    if (!enabled) return;
    can.Poll();
    if (can.Failed() || can.Overflow() != 0) { client.Fail(Fault::Transport); Release(); return; }
    if (connecting) {
        if (!can.Ready()) return;
        connecting = false; client.Start(selectedUuid, StepTimer::GetTickRate(), now);
    }
    CanFrame frame;
    for (unsigned budget = 0; budget < 32 && can.Read(frame); ++budget) {
        if (frame.id == ResponseCanId) {
            const uint64_t drainedAt = Now();
            const uint64_t received = drainedAt - static_cast<uint32_t>(static_cast<uint32_t>(drainedAt) - frame.received);
            client.Receive(frame.data, frame.length, received, drainedAt);
        }
    }
    client.Tick(Now());
    if (client.GetState() == State::Fault || client.GetState() == State::Disabled) Release();
}
void Exit() noexcept { client.Stop(); Release(); }
void EmergencyStop() noexcept { client.Stop(); Release(); }
void Diagnostics(MessageType destination) noexcept {
    String<1024> reply; Status(reply.GetRef());
    reprap.GetPlatform().MessageF(destination, "%s\n", reply.c_str());
}
GCodeResult Configure(GCodeBuffer& gb, const StringRef& reply) THROWS(GCodeException) {
    if (gb.Seen('L')) {
        String<4> language; gb.GetQuotedString(language.GetRef());
        if (std::strcmp(language.c_str(), "en") == 0) ukrainian = false;
        else if (std::strcmp(language.c_str(), "uk") == 0) ukrainian = true;
        else { reply.copy(Text(Message::Language, ukrainian)); return GCodeResult::error; }
    }
    if (!gb.Seen('S')) { Status(reply, false); return GCodeResult::ok; }
    const uint32_t action = gb.GetUIValue();
    if (action > 1) { reply.copy(Text(Message::Parameters, ukrainian)); return GCodeResult::error; }
    if (action == 0) {
        if (connecting || !enabled) Exit();
        else client.RequestStop(Now());
        Status(reply, false); return GCodeResult::ok;
    }
    if (enabled) { reply.copy(Text(Message::Active, ukrainian)); return GCodeResult::error; }
    String<20> requested;
    if (gb.Seen('U')) gb.GetQuotedString(requested.GetRef());
    uint8_t uuid[6] = {};
    bool valid = requested.strlen() == 12;
    if (valid) for (unsigned i = 0; i < 6; ++i) {
        const int high = Hex(requested.c_str()[2*i]), low = Hex(requested.c_str()[2*i+1]);
        if (high < 0 || low < 0) { valid = false; break; }
        uuid[i] = (high << 4) | low;
    }
    if (!valid) { reply.copy(Text(Message::Uuid, ukrainian)); return GCodeResult::error; }
    if (!receivePin.AssignPort("d.0", reply, PinUsedBy::sensor, PinAccess::read) ||
        !transmitPin.AssignPort("d.1", reply, PinUsedBy::sensor, PinAccess::read) ||
        receivePin.GetPin() != PD_0 || transmitPin.GetPin() != PD_1 ||
        receivePin.GetTotalInvert() || transmitPin.GetTotalInvert()) {
        receivePin.Release(); transmitPin.Release(); reply.copy(Text(Message::Pins, ukrainian)); return GCodeResult::error;
    }
    std::memcpy(selectedUuid, uuid, sizeof(uuid));
    client.Stop(); enabled = connecting = true;
    can.Start(); Status(reply, false);
    return GCodeResult::ok;
}
}
