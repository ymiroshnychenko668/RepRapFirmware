#pragma once
#include "Protocol.h"
#include "ClockSync.h"

namespace RrfModules::CartographerV3 {
constexpr uint8_t NodeId = 64;
constexpr uint16_t CommandCanId = 0x100 + NodeId * 2;
constexpr uint16_t ResponseCanId = CommandCanId + 1;
enum class State : uint8_t { Disabled, Identifying, Identity, Configuration, Base, Streaming, Ready, Stopping, Fault };
enum class Fault : uint8_t { None, Transport, Timeout, UnknownProfile, Protocol, Identity, Shutdown, Reboot, Stale, Sensor };
struct Sample {
    uint64_t mcuClock = 0, hostTime = 0;
    uint32_t count = 0, adcSum = 0;
    double frequency = 0, temperature = 0;
    bool valid = false;
};
class Transport {
public:
    virtual bool Send(uint16_t id, const uint8_t* data, size_t length) noexcept = 0;
    virtual ~Transport() = default;
};
class Client {
public:
    explicit Client(Transport& transport) noexcept : transport(transport) { }
    void Start(const uint8_t uuid[6], uint32_t hostRate, uint64_t now) noexcept;
    void Stop() noexcept;
    void RequestStop(uint64_t now) noexcept;
    void Fail(Fault reason) noexcept;
    void Tick(uint64_t now) noexcept;
    void Receive(const uint8_t* data, size_t length, uint64_t received, uint64_t servicedAt) noexcept;
    State GetState() const noexcept { return state; }
    Fault GetFault() const noexcept { return fault; }
    const Profile* GetProfile() const noexcept { return profile; }
    const Sample& LastSample() const noexcept { return lastSample; }
    const ClockSync& Clock() const noexcept { return clock; }
    uint32_t SampleAgeUs(uint64_t now) const noexcept;
    bool HasFreshSample(uint64_t now) const noexcept;
    bool RecentSample(size_t age, Sample& sample) const noexcept;
    uint32_t samples = 0, invalidSamples = 0, overwritten = 0, retries = 0, timeouts = 0, unknownMessages = 0;
    uint32_t BaseCount() const noexcept { return baseCount; }
    uint16_t BaseAdc() const noexcept { return baseAdc; }
    uint32_t BadCrc() const noexcept { return parser.badCrc; }
    uint32_t Malformed() const noexcept { return parser.malformed; }
    static const char* StateName(State state) noexcept;
    static const char* FaultName(Fault fault) noexcept;
private:
    Transport& transport;
    State state = State::Disabled;
    Fault fault = Fault::None;
    const Profile* profile = nullptr;
    BlockParser parser;
    ClockSync clock;
    uint32_t hostHz = 1;
    uint8_t uuid[6] = {}, sequence = 0, retryCount = 0, queryRetries = 0;
    uint8_t tx[MaxBlock] = {}, payload[MaxBlock - 5] = {};
    size_t txSize = 0, txOffset = 0, payloadSize = 0;
    bool pending = false, waitingResponse = false, streamEnabled = false, everReady = false, stopRequested = false, stopSent = false;
    uint64_t now = 0, sentAt = 0, completedAt = 0, requestAt = 0, nextClock = 0, startedAt = 0, lastReceivedSample = 0, lastDataMcu = 0, lastValidReceived = 0, stopAt = 0;
    size_t identifyOffset = 0;
    uint32_t candidates = 0, baseCount = 0;
    uint16_t baseAdc = 0;
    uint8_t consecutiveValid = 0;
    Sample lastSample, history[64];
    size_t historyWrite = 0;
    void Request(uint8_t command, const uint32_t* args, size_t count, bool response) noexcept;
    void IdentifyNext() noexcept;
    void Pump() noexcept;
    void HandleBlock(const uint8_t* block, size_t length, uint64_t received) noexcept;
    void HandleMessage(uint32_t id, const uint32_t* args, const uint8_t* const* buffers, uint64_t received, bool queryResponse) noexcept;
    void Data(const uint32_t* args, uint64_t received) noexcept;
    static void OnBlock(void* context, const uint8_t* block, size_t length, uint64_t received) noexcept;
};
}
