#include <RepRapFirmware.h>
#include "CanTransport.h"
#include <cassert>
#include <cstdio>
using namespace RrfModules::CartographerV3;
extern "C" void CAN1_RX0_IRQHandler() noexcept;
static void Ready(CanTransport& can) {
    fakeCan = {}; fakeMillis = 0; fakePclk = 45000000;
    assert(can.Start() && !can.Ready() && !fakeIrq);
    fakeCan.MSR = CAN_MSR_INAK; can.Poll();
    assert(fakeCan.BTR == (2u | (11u<<16) | (1u<<20)));
    assert(fakeCan.sFilterRegister[0].FR1 == static_cast<uint32_t>(ResponseCanId)<<21);
    assert(fakeCan.sFilterRegister[0].FR2 == 0x3f1u<<21);
    fakeCan.MSR = 0; can.Poll(); assert(can.Ready() && fakeIrq && fakePriority == 12);
}
static void Receive(unsigned value) {
    fakeCan.RF0R = 1; fakeCan.sFIFOMailBox[0].RIR = ResponseCanId << 21;
    fakeCan.sFIFOMailBox[0].RDTR = 4; fakeCan.sFIFOMailBox[0].RDLR = value;
    fakeTicks = value; CAN1_RX0_IRQHandler();
}
int main() {
    CanTransport can;
    assert(!can.Ready() && !can.Send(0,nullptr,0));
    Ready(can);
    const uint8_t data[] = {1,2,3,4,5,6,7,8};
    assert(!can.Send(CommandCanId,data,8));
    fakeCan.TSR = CAN_TSR_TME1;
    assert(can.Send(CommandCanId,data,8));
    assert(fakeCan.sTxMailBox[1].TIR == (static_cast<uint32_t>(CommandCanId)<<21 | 1u));
    assert(fakeCan.sTxMailBox[1].TDLR == 0x04030201 && fakeCan.sTxMailBox[1].TDHR == 0x08070605);
    assert(!can.Send(0x800,data,8) && !can.Send(0,data,9));
    Receive(123456);
    CanFrame frame; assert(can.Read(frame) && frame.received == 123456 && frame.length == 4 && frame.id == ResponseCanId);
    assert(!can.Read(frame));
    for (unsigned i=0;i<64;++i) Receive(i);
    assert(can.Overflow() == 1);
    for (unsigned i=0;i<63;++i) { assert(can.Read(frame)); assert(frame.received == i); }
    assert(!can.Read(frame));
    fakeCan.ESR = CAN_ESR_BOFF; can.Poll(); assert(can.Failed());
    can.Stop(); assert(!fakeIrq && (fakeRcc.APB1ENR & RCC_APB1ENR_CAN1EN) == 0 && can.Errors() == CAN_ESR_BOFF);
    assert((fakeGpio.MODER & 15) == 0);
    fakeCan = {}; assert(can.Start()); fakeMillis = 101; can.Poll(); assert(can.Failed()); can.Stop();
    fakeCan = {}; assert(can.Start()); fakePclk=17000000; fakeCan.MSR=CAN_MSR_INAK; can.Poll(); assert(can.Failed()); can.Stop();
    std::puts("Cartographer bxCAN register model: timing, filters, ISR ring, overflow, bus-off and init timeout passed");
}
