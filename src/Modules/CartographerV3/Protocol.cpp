#include "Protocol.h"
#include <cstring>

namespace RrfModules::CartographerV3 {
uint16_t Crc16(const uint8_t* data, size_t length) noexcept {
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < length; ++i) {
        uint16_t value = data[i] ^ (crc & 0xff);
        value ^= (value & 0x0f) << 4;
        crc = ((value << 8) | (crc >> 8)) ^ (value >> 4) ^ (value << 3);
    }
    return crc;
}
size_t EncodeUInt(uint32_t value, uint8_t* out) noexcept {
    // Match the host's PT_uint32 encoder; the decoder also accepts compact signed MCU bit patterns.
    unsigned groups = 1;
    if (value >= 96) groups = 2;
    if (value >= 12288) groups = 3;
    if (value >= 1572864) groups = 4;
    if (value >= 201326592) groups = 5;
    for (unsigned i = 0; i < groups; ++i) {
        out[i] = ((value >> (7 * (groups - i - 1))) & 0x7f) | ((i + 1 < groups) ? 0x80 : 0);
    }
    return groups;
}
bool DecodeUInt(const uint8_t* data, size_t length, size_t& position, uint32_t& value) noexcept {
    if (position >= length) return false;
    uint8_t byte = data[position++];
    value = byte & 0x7f;
    if ((byte & 0x60) == 0x60) value |= 0xffffffe0u;
    unsigned groups = 1;
    while ((byte & 0x80) != 0) {
        if (position >= length || groups++ >= 5) return false;
        byte = data[position++];
        value = (value << 7) | (byte & 0x7f);
    }
    return true;
}
size_t MakeBlock(uint8_t sequence, const uint8_t* payload, size_t length, uint8_t* out) noexcept {
    if (length > MaxBlock - 5) return 0;
    out[0] = length + 5;
    out[1] = 0x10 | (sequence & 0x0f);
    if (length != 0) std::memcpy(out + 2, payload, length);
    const uint16_t crc = Crc16(out, length + 2);
    out[length + 2] = crc >> 8;
    out[length + 3] = crc;
    out[length + 4] = 0x7e;
    return length + 5;
}
void BlockParser::Feed(const uint8_t* data, size_t length, uint64_t received, Callback callback, void* context) noexcept {
    for (size_t i = 0; i < length; ++i) {
        buffer[used++] = data[i];
        while (used != 0) {
            const size_t wanted = buffer[0];
            bool discard = wanted < 5 || wanted > MaxBlock;
            if (!discard && used >= 2) discard = (buffer[1] & 0xf0) != 0x10;
            if (!discard && used < wanted) break;
            if (!discard) {
                const uint16_t crc = Crc16(buffer, wanted - 3);
                if (buffer[wanted - 1] == 0x7e && buffer[wanted - 3] == (crc >> 8) && buffer[wanted - 2] == (crc & 0xff)) {
                    callback(context, buffer, wanted, received);
                    used -= wanted;
                    std::memmove(buffer, buffer + wanted, used);
                    continue;
                }
                ++badCrc;
            } else ++malformed;
            --used;
            std::memmove(buffer, buffer + 1, used);
        }
    }
}
}
