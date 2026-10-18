#pragma once
#include "Client.h"

namespace RrfModules::CartographerV3 {
struct CanFrame { uint32_t received; uint16_t id; uint8_t length; uint8_t data[8]; };
class CanTransport final : public Transport {
public:
    bool Start() noexcept;
    void Stop() noexcept;
    void Poll() noexcept;
    bool Read(CanFrame& frame) noexcept;
    bool Send(uint16_t id, const uint8_t* data, size_t length) noexcept override;
    bool Ready() const noexcept { return phase == 3; }
    bool Failed() const noexcept { return phase == 4; }
    void Interrupt() noexcept;
    uint32_t RxCount() const noexcept { return rxCount; }
    uint32_t Overflow() const noexcept { return overflow; }
    uint32_t Errors() const noexcept;
private:
    CanFrame frames[64] = {};
    volatile uint32_t head = 0, tail = 0, rxCount = 0, overflow = 0;
    uint32_t started = 0, lastErrors = 0;
    uint8_t phase = 0;
};
}
