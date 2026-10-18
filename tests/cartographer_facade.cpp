#include "CartographerV3.h"
#include <GCodes/GCodeBuffer/GCodeBuffer.h>
#include <Hardware/IoPorts.h>
#include <Platform/Platform.h>
#include <cassert>
#include <cstdio>
using namespace RrfModules::CartographerV3;
static std::string reply;
static GCodeResult Call(std::initializer_list<std::pair<const char,std::string>> parameters) {
    GCodeBuffer buffer(parameters); StringRef output(reply,1024); return Configure(buffer,output);
}
int main() {
    Init(); Spin(); assert(fakeOwnedPins == 0 && fakeRcc.APB1ENR == 0);
    assert(Call({}) == GCodeResult::ok && reply.find("state=disabled") != std::string::npos);
    assert(Call({{'S',"1"}}) == GCodeResult::error && reply.find("UUID") != std::string::npos);
    assert(Call({{'S',"1"},{'U',"123456789abz"}}) == GCodeResult::error && fakeOwnedPins == 0);
    fakePinConflict = true;
    assert(Call({{'S',"1"},{'U',"123456789abc"}}) == GCodeResult::error && fakeOwnedPins == 0);
    fakePinConflict = false;
    fakeWrongAlias = true;
    assert(Call({{'S',"1"},{'U',"123456789abc"}}) == GCodeResult::error && fakeOwnedPins == 0 && fakeRcc.APB1ENR == 0);
    fakeWrongAlias = false;
    assert(Call({{'S',"1"},{'U',"123456789abc"}}) == GCodeResult::ok && fakeOwnedPins == 2);
    assert((fakeRcc.APB1ENR & RCC_APB1ENR_CAN1EN) != 0);
    const uint32_t control = fakeCan.MCR;
    assert(Call({{'L',"uk"}}) == GCodeResult::ok && reply.find("can_starting") != std::string::npos);
    assert(fakeOwnedPins == 2 && fakeCan.MCR == control); // locale update preserves the active connection
    assert(Call({{'S',"1"},{'U',"123456789abc"}}) == GCodeResult::error && reply.find("зупиніть") != std::string::npos);
    assert(Call({{'S',"2"}}) == GCodeResult::error && reply.find("має бути") != std::string::npos);
    assert(fakeOwnedPins == 2 && fakeCan.MCR == control);
    assert(Call({{'L',"en"}}) == GCodeResult::ok && fakeOwnedPins == 2 && fakeCan.MCR == control);
    assert(Call({{'S',"2"}}) == GCodeResult::error && reply.find("must be") != std::string::npos);
    assert(Call({{'L',"zz"}}) == GCodeResult::error && fakeOwnedPins == 2);
    Diagnostics(0); assert(fakeDiagnostics.find("clock_samples=") != std::string::npos);
    EmergencyStop(); assert(fakeOwnedPins == 0 && !fakeIrq && (fakeRcc.APB1ENR & RCC_APB1ENR_CAN1EN) == 0);
    assert(Call({{'S',"1"},{'U',"123456789abc"}}) == GCodeResult::ok);
    Init(); assert(fakeOwnedPins == 0); // lifecycle reinitialization releases the old session
    assert(Call({{'S',"0"}}) == GCodeResult::ok && reply.find("disabled") != std::string::npos);
    std::puts("Cartographer command facade: UUID/pin validation, en/uk, preserved connection, emergency stop and re-init passed");
}
