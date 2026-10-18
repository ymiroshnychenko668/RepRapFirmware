// Native F446 bxCAN, independent of Duet CAN-FD and CoreN2G SUPPORT_CAN.
#include <RepRapFirmware.h>
#include <CoreIO.h>
#include <Movement/StepTimer.h>
#include "CanTransport.h"
#include <cstring>

#if !defined(OCTOPUS_F446)
# error "cartographer-v3 diagnostic CAN transport requires Octopus F446"
#endif

namespace RrfModules::CartographerV3 {
static CanTransport* active = nullptr;
bool CanTransport::Start() noexcept {
    Stop();
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    RCC->APB1ENR |= RCC_APB1ENR_CAN1EN;
    RCC->APB1RSTR |= RCC_APB1RSTR_CAN1RST;
    RCC->APB1RSTR &= ~RCC_APB1RSTR_CAN1RST;
    // Octopus V1.1: PD0 CAN1_RX, PD1 CAN1_TX, AF9. Pins are reserved by IoPort first.
    GPIOD->MODER = (GPIOD->MODER & ~0x0fu) | 0x0au;
    GPIOD->OTYPER &= ~3u;
    GPIOD->PUPDR = (GPIOD->PUPDR & ~0x0fu) | 0x01u;
    GPIOD->OSPEEDR = (GPIOD->OSPEEDR & ~0x0fu) | 0x0fu;
    GPIOD->AFR[0] = (GPIOD->AFR[0] & ~0xffu) | 0x99u;
    head = tail = rxCount = overflow = 0; lastErrors = 0;
    active = this;
    CAN1->MCR = CAN_MCR_INRQ | CAN_MCR_TXFP; // chronological mailbox ordering; no automatic bus-off recovery
    started = millis(); phase = 1;
    return true;
}
void CanTransport::Poll() noexcept {
    if (phase == 0 || phase == 4) return;
    if ((CAN1->ESR & CAN_ESR_BOFF) != 0) { phase = 4; return; }
    if (phase == 3) return;
    if (millis() - started > 100) { phase = 4; return; }
    if (phase == 1 && (CAN1->MSR & CAN_MSR_INAK) != 0) {
        const uint32_t peripheralHz = HAL_RCC_GetPCLK1Freq();
        if (peripheralHz % 15000000 != 0 || peripheralHz / 15000000 == 0) { phase = 4; return; }
        // 15 time quanta, 86.7% sample point, 1 Mbit/s (45 MHz APB1 -> prescaler 3).
        CAN1->BTR = (peripheralHz / 15000000 - 1) | (11u << 16) | (1u << 20);
        CAN1->FMR |= CAN_FMR_FINIT;
        CAN1->FA1R &= ~1u;
        CAN1->FS1R |= 1u; CAN1->FM1R |= 1u; CAN1->FFA1R &= ~1u;
        CAN1->sFilterRegister[0].FR1 = ResponseCanId << 21;
        CAN1->sFilterRegister[0].FR2 = 0x3f1u << 21;
        CAN1->FA1R |= 1u; CAN1->FMR &= ~CAN_FMR_FINIT;
        CAN1->MCR &= ~CAN_MCR_INRQ;
        phase = 2;
    }
    if (phase == 2 && (CAN1->MSR & CAN_MSR_INAK) == 0) {
        CAN1->IER = CAN_IER_FMPIE0 | CAN_IER_FOVIE0;
        NVIC_SetPriority(CAN1_RX0_IRQn, 12); // lower urgency than step generation; no RTOS APIs in this ISR
        NVIC_ClearPendingIRQ(CAN1_RX0_IRQn); NVIC_EnableIRQ(CAN1_RX0_IRQn);
        phase = 3;
    }
}
void CanTransport::Stop() noexcept {
    if (phase != 0) {
        lastErrors = CAN1->ESR;
        NVIC_DisableIRQ(CAN1_RX0_IRQn); NVIC_ClearPendingIRQ(CAN1_RX0_IRQn);
        CAN1->IER = 0;
        CAN1->TSR = CAN_TSR_ABRQ0 | CAN_TSR_ABRQ1 | CAN_TSR_ABRQ2;
        CAN1->MCR |= CAN_MCR_INRQ;
        RCC->APB1ENR &= ~RCC_APB1ENR_CAN1EN;
        GPIOD->MODER &= ~0x0fu; // release to inputs before IoPort ownership is released
    }
    if (active == this) active = nullptr;
    phase = 0;
}
bool CanTransport::Send(uint16_t id, const uint8_t* data, size_t length) noexcept {
    if (!Ready() || id > 0x7ff || length > 8) return false;
    const uint32_t status = CAN1->TSR;
    unsigned mailbox;
    if (status & CAN_TSR_TME0) mailbox = 0;
    else if (status & CAN_TSR_TME1) mailbox = 1;
    else if (status & CAN_TSR_TME2) mailbox = 2;
    else return false;
    uint32_t words[2] = {};
    if (length != 0) std::memcpy(words, data, length);
    CAN1->sTxMailBox[mailbox].TDTR = length;
    CAN1->sTxMailBox[mailbox].TDLR = words[0]; CAN1->sTxMailBox[mailbox].TDHR = words[1];
    CAN1->sTxMailBox[mailbox].TIR = (static_cast<uint32_t>(id) << 21) | CAN_TI0R_TXRQ;
    return true;
}
void CanTransport::Interrupt() noexcept {
    if ((CAN1->RF0R & CAN_RF0R_FOVR0) != 0) { ++overflow; CAN1->RF0R = CAN_RF0R_FOVR0; }
    for (unsigned budget = 0; budget < 3 && (CAN1->RF0R & CAN_RF0R_FMP0) != 0; ++budget) {
        const uint32_t id = CAN1->sFIFOMailBox[0].RIR;
        const uint8_t length = CAN1->sFIFOMailBox[0].RDTR & 0x0f;
        const uint32_t next = (head + 1) & 63;
        if (length > 8 || (id & (CAN_RI0R_IDE | CAN_RI0R_RTR)) != 0 || next == tail) ++overflow;
        else {
            CanFrame& frame = frames[head];
            frame.id = id >> 21; frame.length = length; frame.received = StepTimer::GetTimerTicks();
            const uint32_t words[] = {CAN1->sFIFOMailBox[0].RDLR, CAN1->sFIFOMailBox[0].RDHR};
            std::memcpy(frame.data, words, 8);
            __DMB(); head = next; ++rxCount;
        }
        CAN1->RF0R = CAN_RF0R_RFOM0;
    }
}
bool CanTransport::Read(CanFrame& frame) noexcept {
    if (tail == head) return false;
    __DMB(); frame = frames[tail]; __DMB(); tail = (tail + 1) & 63;
    return true;
}
uint32_t CanTransport::Errors() const noexcept { return phase != 0 ? CAN1->ESR : lastErrors; }
void CanRxInterrupt() noexcept { if (active != nullptr) active->Interrupt(); }
}
extern "C" void CAN1_RX0_IRQHandler() noexcept { RrfModules::CartographerV3::CanRxInterrupt(); }
