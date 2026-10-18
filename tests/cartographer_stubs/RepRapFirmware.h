#pragma once
#include <cstdint>
#include <atomic>
#include <string>
#include <cstdarg>
#include <cstdio>
#define THROWS(...)
enum class GCodeResult { ok, error };
enum class PinUsedBy { sensor };
enum class PinAccess { read };
class StringRef {
    std::string& value;
    size_t maximum;
public:
    StringRef(std::string& value, size_t maximum) : value(value), maximum(maximum) { }
    void copy(const char* text) const { value.assign(text, std::char_traits<char>::length(text) < maximum ? std::char_traits<char>::length(text) : maximum); }
    void printf(const char* format, ...) const {
        char text[2048]; va_list args; va_start(args,format); vsnprintf(text,sizeof(text),format,args); va_end(args); copy(text);
    }
    void catf(const char* format, ...) const {
        char text[2048]; va_list args; va_start(args,format); vsnprintf(text,sizeof(text),format,args); va_end(args); value += text;
        if (value.size() > maximum) value.resize(maximum);
    }
};
template<size_t Size> class String {
    std::string value;
public:
    StringRef GetRef() { return StringRef(value,Size); }
    const char* c_str() const { return value.c_str(); }
    size_t strlen() const { return value.size(); }
};
// Minimal register model for compiling the production bxCAN translation unit.
struct FakeTxMailbox { volatile uint32_t TIR=0, TDTR=0, TDLR=0, TDHR=0; };
struct FakeRxMailbox { volatile uint32_t RIR=0, RDTR=0, RDLR=0, RDHR=0; };
struct FakeFilter { volatile uint32_t FR1=0, FR2=0; };
struct FakeCan {
    volatile uint32_t MCR=0, MSR=0, TSR=0, RF0R=0, ESR=0, BTR=0, IER=0;
    volatile uint32_t FMR=0, FA1R=0, FS1R=0, FM1R=0, FFA1R=0;
    FakeTxMailbox sTxMailBox[3]; FakeRxMailbox sFIFOMailBox[2]; FakeFilter sFilterRegister[28];
};
struct FakeRcc { volatile uint32_t AHB1ENR=0, APB1ENR=0, APB1RSTR=0; };
struct FakeGpio { volatile uint32_t MODER=0, OTYPER=0, PUPDR=0, OSPEEDR=0, AFR[2]={}; };
inline FakeCan fakeCan;
inline FakeRcc fakeRcc;
inline FakeGpio fakeGpio;
inline auto CAN1 = &fakeCan;
inline auto RCC = &fakeRcc;
inline auto GPIOD = &fakeGpio;
inline uint32_t fakeMillis = 0, fakeTicks = 0, fakePclk = 45000000;
inline bool fakeIrq = false;
inline unsigned fakePriority = 0;
constexpr unsigned CAN1_RX0_IRQn = 20;
inline void NVIC_SetPriority(unsigned, unsigned p) { fakePriority=p; }
inline void NVIC_ClearPendingIRQ(unsigned) { }
inline void NVIC_EnableIRQ(unsigned) { fakeIrq=true; }
inline void NVIC_DisableIRQ(unsigned) { fakeIrq=false; }
inline void __DMB() { std::atomic_thread_fence(std::memory_order_seq_cst); }
constexpr uint32_t RCC_AHB1ENR_GPIODEN=1u<<3, RCC_APB1ENR_CAN1EN=1u<<25, RCC_APB1RSTR_CAN1RST=1u<<25;
constexpr uint32_t CAN_MCR_INRQ=1u, CAN_MCR_TXFP=1u<<2, CAN_ESR_BOFF=1u<<2, CAN_MSR_INAK=1u;
constexpr uint32_t CAN_FMR_FINIT=1u, CAN_IER_FMPIE0=1u<<1, CAN_IER_FOVIE0=1u<<3;
constexpr uint32_t CAN_TSR_ABRQ0=1u<<7, CAN_TSR_ABRQ1=1u<<15, CAN_TSR_ABRQ2=1u<<23;
constexpr uint32_t CAN_TSR_TME0=1u<<26, CAN_TSR_TME1=1u<<27, CAN_TSR_TME2=1u<<28;
constexpr uint32_t CAN_TI0R_TXRQ=1u, CAN_RF0R_FOVR0=1u<<4, CAN_RF0R_FMP0=3u, CAN_RF0R_RFOM0=1u<<5;
constexpr uint32_t CAN_RI0R_IDE=1u<<2, CAN_RI0R_RTR=1u<<1;
