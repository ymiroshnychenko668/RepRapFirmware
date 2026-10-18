// Cartographer diagnostic transport. GPL-3.0-or-later.
// Wire format follows Klipper (Kevin O'Connor, GPLv3); see README.md.
#pragma once
#include <cstddef>
#include <cstdint>

namespace RrfModules::CartographerV3 {
constexpr size_t MaxBlock = 64;
struct ResponseFormat { uint8_t id, count, buffers; };
struct Profile {
    const char* name;
    const uint8_t* dictionary;
    size_t dictionarySize;
    const ResponseFormat* responses;
    size_t responseCount;
    uint32_t clockHz, adcMax, smoothCount, periodUs;
    uint8_t getConfig, getUptime, baseRead, stream, getId;
    uint8_t configResponse, uptimeResponse, baseResponse, dataResponse, idResponse;
    uint8_t startingResponse, shutdownResponse, isShutdownResponse;
};
extern const Profile Profiles[];
extern const size_t ProfileCount;

uint16_t Crc16(const uint8_t* data, size_t length) noexcept;
size_t EncodeUInt(uint32_t value, uint8_t* out) noexcept;
bool DecodeUInt(const uint8_t* data, size_t length, size_t& position, uint32_t& value) noexcept;
size_t MakeBlock(uint8_t sequence, const uint8_t* payload, size_t length, uint8_t* out) noexcept;

class BlockParser {
public:
    using Callback = void (*)(void*, const uint8_t*, size_t, uint64_t) noexcept;
    void Reset() noexcept { used = 0; badCrc = malformed = 0; }
    void Feed(const uint8_t* data, size_t length, uint64_t received, Callback callback, void* context) noexcept;
    uint32_t badCrc = 0, malformed = 0;
private:
    uint8_t buffer[MaxBlock] = {};
    size_t used = 0;
};
}
