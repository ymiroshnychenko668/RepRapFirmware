#include "Client.h"
#include "Messages.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <set>
#include <vector>

using namespace RrfModules::CartographerV3;
static const uint8_t Uuid[] = {0x12,0x34,0x56,0x78,0x9a,0xbc};
struct Packet { uint16_t id; std::vector<uint8_t> data; };
struct FakeTransport final : Transport {
    std::deque<Packet> packets;
    unsigned capacity = 1000;
    bool Send(uint16_t id, const uint8_t* bytes, size_t length) noexcept override {
        if (capacity == 0) return false;
        --capacity; packets.push_back({id, {bytes, bytes + length}}); return true;
    }
};
struct Peer {
    FakeTransport transport;
    Client client{transport};
    BlockParser parser;
    const Profile& profile;
    std::vector<uint8_t> dictionary;
    uint64_t now = 1000000, clockBase = 480000000;
    uint8_t nextSequence = 0;
    bool streaming = false, shutdown = false, wrongUuid = false, noResponses = false, dropIdentify = false, dropCommand = false;
    unsigned streamEnables = 0, capacity = 1000;
    std::set<uint32_t> commands;
    explicit Peer(size_t index = 0) : profile(Profiles[index]), dictionary(profile.dictionary, profile.dictionary + profile.dictionarySize) { }
    uint64_t Mcu() const { return clockBase + 48 * now; }
    void Emit(const std::vector<uint8_t>& payload) {
        uint8_t block[64]; const size_t size = MakeBlock(nextSequence, payload.data(), payload.size(), block);
        assert(size != 0);
        // CAN packet boundaries never define Klipper message boundaries.
        for (size_t offset = 0; offset < size;) {
            const size_t length = (size - offset > 7 ? 7 : size - offset);
            client.Receive(block + offset, length, now, now); offset += length;
        }
    }
    static void UInt(std::vector<uint8_t>& data, uint32_t value) {
        uint8_t out[5]; const size_t size = EncodeUInt(value, out); data.insert(data.end(), out, out + size);
    }
    void Response(uint8_t id, std::initializer_list<uint32_t> args) {
        std::vector<uint8_t> data; UInt(data, id); for (auto value : args) UInt(data, value); Emit(data);
    }
    static void Command(void* context, const uint8_t* block, size_t length, uint64_t) noexcept {
        static_cast<Peer*>(context)->Handle(block, length);
    }
    void Handle(const uint8_t* block, size_t length) {
        if (noResponses) return;
        if ((block[1] & 15) != nextSequence) { Emit({}); return; }
        nextSequence = (nextSequence + 1) & 15;
        size_t pos = 2; uint32_t id;
        assert(DecodeUInt(block, length - 3, pos, id)); commands.insert(id);
        auto arg = [&] { uint32_t value; assert(DecodeUInt(block, length - 3, pos, value)); return value; };
        if (dropCommand) { dropCommand = false; return; } // command consumed, both ACK and response lost
        if (id == 1) {
            const uint32_t offset = arg(), count = arg();
            if (dropIdentify) { dropIdentify = false; Emit({}); return; }
            assert(offset <= dictionary.size());
            const size_t take = dictionary.size() - offset < count ? dictionary.size() - offset : count;
            std::vector<uint8_t> response; UInt(response, 0); UInt(response, offset);
            response.push_back(take); response.insert(response.end(), dictionary.begin() + offset, dictionary.begin() + offset + take);
            Emit(response);
        } else if (id == profile.getId) {
            std::vector<uint8_t> response; UInt(response, profile.idResponse); response.push_back(6);
            response.insert(response.end(), Uuid, Uuid + 6); if (wrongUuid) response[2] ^= 1;
            UInt(response, NodeId); Emit(response);
        } else if (id == profile.getConfig) Response(profile.configResponse, {0, 0, shutdown ? 1u : 0u, 0});
        else if (id == profile.baseRead) {
            assert(arg() == 6 && arg() == 0);
            std::vector<uint8_t> response; UInt(response, profile.baseResponse); response.push_back(6);
            const uint32_t count = 32917500; const uint16_t adc = 55927;
            for (unsigned n = 0; n < 4; ++n) response.push_back(count >> (8*n));
            response.push_back(static_cast<uint8_t>(adc)); response.push_back(adc >> 8); UInt(response, 0); Emit(response);
        } else if (id == profile.stream) { streaming = arg() != 0; ++streamEnables; Emit({}); }
        else if (id == profile.getUptime) Response(profile.uptimeResponse, {static_cast<uint32_t>(Mcu() >> 32), static_cast<uint32_t>(Mcu())});
        else assert(false && "diagnostic client issued an unapproved command");
    }
    void Start() { transport.capacity = capacity; client.Start(Uuid, 1000000, now); }
    void Pump() {
        unsigned budget = 0;
        while (!transport.packets.empty()) {
            assert(++budget < 1000);
            Packet packet = transport.packets.front(); transport.packets.pop_front();
            if (packet.id == 0x3f0) {
                assert(packet.data.size() == 8 && packet.data[0] == 1 && packet.data[7] == NodeId);
                assert(std::memcmp(packet.data.data() + 1, Uuid, 6) == 0);
            } else { assert(packet.id == CommandCanId); parser.Feed(packet.data.data(), packet.data.size(), now, Command, this); }
        }
    }
    void Sample(uint32_t count = 32917500, uint32_t adc = 55927) { Response(profile.dataResponse, {static_cast<uint32_t>(Mcu()), count, adc}); }
    void Step(bool data = true) {
        now += 1000; transport.capacity = capacity;
        client.Tick(now); Pump();
        if (data && streaming && now % profile.periodUs == 0) Sample();
    }
    void Run(unsigned ms, bool data = true) { for (unsigned i = 0; i < ms; ++i) Step(data); }
};

static void TestEncoding() {
    const uint32_t values[] = {0, 31, 95, 96, 12287, 12288, 1572863, 1572864, 201326591, 201326592, 0x7fffffff, 0x80000000, 0xffffffdf, 0xffffffe0, 0xffffffff};
    for (uint32_t value : values) {
        uint8_t bytes[5]; const size_t size = EncodeUInt(value, bytes); size_t pos = 0; uint32_t decoded;
        assert(DecodeUInt(bytes, size, pos, decoded) && pos == size && decoded == value);
        if (size > 1) { pos = 0; assert(!DecodeUInt(bytes, size - 1, pos, decoded)); }
    }
    uint8_t tooLong[] = {0x80,0x80,0x80,0x80,0x80,0}; size_t pos = 0; uint32_t decoded;
    assert(!DecodeUInt(tooLong, sizeof(tooLong), pos, decoded));
    const uint8_t digits[] = {'1','2','3','4','5','6','7','8','9'};
    assert(Crc16(digits, sizeof(digits)) == 0x6f91); // CRC-16/MCRF4XX, as used by Klipper
    uint8_t encoded[64]; const uint8_t payload[] = {1, 0, 40};
    assert(MakeBlock(0, payload, sizeof(payload), encoded) == 8);
    assert(encoded[0] == 8 && encoded[1] == 0x10 && encoded[7] == 0x7e);
    assert(MakeBlock(0, encoded, 60, encoded) == 0);
    const uint8_t maximum[] = {0x8f,0xff,0xff,0xff,0x7f};
    assert(EncodeUInt(UINT32_MAX, encoded) == 5 && std::memcmp(encoded, maximum, 5) == 0);
    const uint8_t compactMinusOne[] = {0x7f}; pos = 0;
    assert(DecodeUInt(compactMinusOne, 1, pos, decoded) && decoded == UINT32_MAX);
}
static void TestParser() {
    struct Observer { unsigned count = 0; static void Block(void* p, const uint8_t*, size_t, uint64_t) noexcept { ++static_cast<Observer*>(p)->count; } } observer;
    BlockParser parser; uint8_t good[64]; const uint8_t data[] = {72, 0x7e, 0x7e, 0};
    const size_t size = MakeBlock(9, data, sizeof(data), good);
    uint8_t bad[64]; std::memcpy(bad, good, size); bad[size - 2] ^= 1;
    parser.Feed(bad, size, 0, Observer::Block, &observer);
    // Enough zero padding flushes any partial bogus header after corruption.
    uint8_t zeros[64] = {}; parser.Feed(zeros, sizeof(zeros), 0, Observer::Block, &observer);
    for (size_t i = 0; i < size; ++i) parser.Feed(good + i, 1, 0, Observer::Block, &observer);
    assert(observer.count == 1 && parser.badCrc != 0);
    uint32_t random = 1;
    for (unsigned i = 0; i < 30000; ++i) { random = random * 1664525u + 1013904223u; const uint8_t byte = random >> 24; parser.Feed(&byte, 1, 0, Observer::Block, &observer); }
}
static void TestClock() {
    ClockSync clock; clock.Reset(1000000, 48000000);
    const uint64_t base = 0xfffffff0ull;
    for (uint64_t i = 0; i < 20; ++i) assert(clock.Observe(base + i * 9600000, 1000000 + i * 200000, 1000200 + i * 200000));
    assert(clock.Ready(4800200));
    uint64_t expanded, host;
    assert(clock.Convert(static_cast<uint32_t>(base + 19 * 9600000 + 48000), expanded, host));
    assert(expanded == base + 19 * 9600000 + 48000 && std::llabs(static_cast<int64_t>(host) - 4801100) <= 2);
    assert(clock.UncertaintyUs(4800200) >= 100);
    assert(!clock.Ready(8000000));
    assert(!clock.Observe(0, 5000000, 5000200) && clock.Rebooted());
    clock.Reset(1000000,48000000);
    assert(!clock.Observe(1,1000000,1020001)); // excessive RTT
    // More than one complete MCU wrap with correct explicit high bits.
    for (uint64_t i = 0; i < 1500; ++i) assert(clock.Observe(i * 9600000, 1000000 + i * 200000, 1000200 + i * 200000));
    assert(clock.Ready(300800200));
}
static void TestPeer(size_t profile) {
    Peer peer(profile); peer.Start(); peer.Run(2000);
    assert(peer.client.GetState() == State::Ready && peer.client.HasFreshSample(peer.now));
    assert(peer.client.GetProfile() == &Profiles[profile]);
    assert(peer.streamEnables == 1 && peer.client.samples > 300 && peer.client.overwritten > 0);
    // Golden values from pinned Klipper extras.thermistor and Cartographer's frequency conversion.
    assert(peer.client.LastSample().valid && std::fabs(peer.client.LastSample().frequency - 2943053.8415908813) < 1);
    assert(std::fabs(peer.client.LastSample().temperature - 20.334694110249814) < 0.001);
    assert(peer.client.BaseCount() == 32917500 && peer.client.BaseAdc() == 55927);
    Sample recent, older;
    assert(peer.client.RecentSample(0, recent) && peer.client.RecentSample(63, older));
    assert(recent.mcuClock > older.mcuClock && !peer.client.RecentSample(64, older));
    const std::set<uint32_t> allowed = {1, peer.profile.getId, peer.profile.getConfig, peer.profile.baseRead, peer.profile.stream, peer.profile.getUptime};
    assert(peer.commands == allowed);
    peer.Run(120, false);
    assert(peer.client.GetFault() == Fault::Stale && !peer.client.LastSample().valid);
    peer.Sample(); assert(peer.client.GetState() == State::Fault); // no silent reconnect
}
static void TestFaults() {
    { Peer p; p.dictionary[20] ^= 1; p.Start(); p.Run(100); assert(p.client.GetFault() == Fault::UnknownProfile && p.streamEnables == 0); }
    { Peer p; p.wrongUuid = true; p.Start(); p.Run(100); assert(p.client.GetFault() == Fault::Identity && p.streamEnables == 0); }
    { Peer p; p.shutdown = true; p.Start(); p.Run(100); assert(p.client.GetFault() == Fault::Shutdown && p.streamEnables == 0); }
    { Peer p; p.noResponses = true; p.Start(); p.Run(800); assert(p.client.GetFault() == Fault::Timeout && p.client.retries >= 4); }
    { Peer p; p.dropIdentify = true; p.Start(); p.Run(2700); assert(p.client.GetState() == State::Ready && p.client.retries != 0); }
    { Peer p; p.dropCommand = true; p.Start(); p.Run(2700); assert(p.client.GetState() == State::Ready && p.client.retries >= 2); }
    { Peer p; p.capacity = 1; p.Start(); p.Run(3500); assert(p.client.GetState() == State::Ready); }
    { Peer p; p.nextSequence = 7; p.Start(); p.Run(2000); assert(p.client.GetState() == State::Ready && p.client.retries != 0); }
    { Peer p; p.Start(); p.Run(2000); p.Response(p.profile.startingResponse, {}); assert(p.client.GetFault() == Fault::Reboot); }
    { Peer p; p.Start(); p.Run(2000); p.Response(p.profile.shutdownResponse, {0,1}); assert(p.client.GetFault() == Fault::Shutdown); }
    { Peer p; p.Start(); p.Run(2000); p.clockBase = 0; p.Run(250); assert(p.client.GetFault() == Fault::Reboot); }
    { Peer p; p.Start(); p.Run(2000); p.Sample(0); assert(!p.client.LastSample().valid && !p.client.HasFreshSample(p.now)); p.Run(10); assert(p.client.GetState() == State::Ready); }
    { Peer p; p.Start(); p.Run(2000); p.Sample(0x0fffffff); assert(!p.client.LastSample().valid); p.Sample(32917500,65520); assert(!p.client.LastSample().valid); }
    { Peer p; p.Start(); p.Run(2000); p.Response(95, {}); assert(p.client.GetFault() == Fault::Protocol); }
    { Peer p; p.Start(); p.Run(2000); p.client.Fail(Fault::Transport); p.client.Fail(Fault::Protocol); assert(p.client.GetFault() == Fault::Transport); }
    { Peer p; p.Start(); p.Run(2000); p.client.RequestStop(p.now); p.Run(5); assert(p.client.GetState() == State::Disabled && !p.streaming); }
    { Peer p; p.Start(); p.Run(2000); p.noResponses = true; p.client.RequestStop(p.now); p.Run(600); assert(p.client.GetState() == State::Disabled || p.client.GetState() == State::Fault); }
    { Peer p; p.Start(); p.Run(2000); const auto state = p.client.GetState(); assert(std::strcmp(Text(Message::Uuid,false),Text(Message::Uuid,true)) != 0); assert(p.client.GetState() == state); p.client.Stop(); assert(p.client.GetState() == State::Disabled && !p.client.LastSample().valid); }
}
int main() {
    TestEncoding(); TestParser(); TestClock(); TestPeer(0); TestPeer(1); TestFaults();
    std::puts("Cartographer: protocol, real Full/Lite dictionaries, fake MCU, clock wraps, faults and bounded backpressure passed");
}
