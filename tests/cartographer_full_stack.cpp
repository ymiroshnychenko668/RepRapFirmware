// Full production module facade -> real bxCAN driver -> unchanged MCU C peer ->
// real CAN RX ISR -> production Client/clock/model -> GCode diagnostics.
// The register adapter models mailbox occupancy and FIFO delivery, not electrical CAN.
#include "CartographerV3.h"
#include "Client.h"
#include "CalibrationModel.h"
#include "cartographer_model_data.h"
#include "cartographer_upstream/peer.h"
#include <GCodes/GCodeBuffer/GCodeBuffer.h>
#include <Hardware/IoPorts.h>
#include <Platform/Platform.h>
#include <RTOSIface/RTOSIface.h>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <future>
#include <thread>
#include <chrono>
#include <string>
#include <vector>
using namespace RrfModules::CartographerV3;
extern "C" void CAN1_RX0_IRQHandler() noexcept;
struct Frame { uint16_t id; std::vector<uint8_t> bytes; unsigned mailbox; };
static std::deque<Frame> transmitted, received;
static uint64_t hostTicks=0x100000000ull-4250000;
static uint32_t sampleCount=33554433, samplePeriodUs=2000;
static uint64_t nextSample=hostTicks;
static unsigned txFrames=0, rxFrames=0;
static bool suppressReports=false, suppressSamples=false, sampleOn=true;
static std::string reply;
static constexpr uint32_t FreeMailboxes=CAN_TSR_TME0|CAN_TSR_TME1|CAN_TSR_TME2;
void FakeCanTxRequested(const volatile FakeTxRequestRegister* source,uint32_t value) noexcept {
    if ((value&CAN_TI0R_TXRQ)==0) return;
    assert((fakeCan.MCR&CAN_MCR_TXFP)!=0);
    unsigned mailbox=3;
    for (unsigned i=0;i<3;++i) if (source==&fakeCan.sTxMailBox[i].TIR) mailbox=i;
    assert(mailbox<3 && (fakeCan.TSR&(CAN_TSR_TME0<<mailbox))!=0);
    const auto& m=fakeCan.sTxMailBox[mailbox];
    const uint32_t words[]={m.TDLR,m.TDHR}; const auto* bytes=reinterpret_cast<const uint8_t*>(words);
    assert(m.TDTR<=8); transmitted.push_back({static_cast<uint16_t>(value>>21),{bytes,bytes+m.TDTR},mailbox});
    fakeCan.TSR&=~(CAN_TSR_TME0<<mailbox); ++txFrames;
}
extern "C" void peer_output(uint16_t id,const uint8_t* bytes,unsigned length) {
    if (!suppressReports) received.push_back({id,{bytes,bytes+length},0});
}
static GCodeResult Call(std::initializer_list<std::pair<const char,std::string>> parameters) {
    GCodeBuffer buffer(parameters); StringRef output(reply,2048); return Configure(buffer,output);
}
static void SyncRegisters() {
    fakeTicks=static_cast<uint32_t>(hostTicks); fakeMillis=static_cast<uint32_t>(hostTicks/1000);
    const uint32_t aborts=CAN_TSR_ABRQ0|CAN_TSR_ABRQ1|CAN_TSR_ABRQ2;
    if ((fakeCan.TSR&aborts)!=0) { transmitted.clear(); fakeCan.TSR=FreeMailboxes; }
    if ((fakeCan.MCR&CAN_MCR_INRQ)!=0) fakeCan.TSR=FreeMailboxes;
    // Explicit host model of the bxCAN init acknowledge; no wall-clock delay.
    fakeCan.MSR=(fakeCan.MCR&CAN_MCR_INRQ)?CAN_MSR_INAK:0;
}
static void DeliverOne(const Frame& frame) {
    if (!fakeIrq || (fakeRcc.APB1ENR&RCC_APB1ENR_CAN1EN)==0) return;
    // Hardware filter list admits only the selected node response/admin IDs.
    const uint32_t encoded=static_cast<uint32_t>(frame.id)<<21;
    if (encoded!=fakeCan.sFilterRegister[0].FR1 && encoded!=fakeCan.sFilterRegister[0].FR2) return;
    assert(frame.bytes.size()<=8);
    uint32_t words[2]={}; std::memcpy(words,frame.bytes.data(),frame.bytes.size());
    fakeCan.sFIFOMailBox[0].RIR=encoded; fakeCan.sFIFOMailBox[0].RDTR=frame.bytes.size();
    fakeCan.sFIFOMailBox[0].RDLR=words[0]; fakeCan.sFIFOMailBox[0].RDHR=words[1];
    fakeCan.RF0R=1; CAN1_RX0_IRQHandler(); ++rxFrames;
    assert(fakeCan.RF0R==CAN_RF0R_RFOM0); // stub acknowledges one scripted FIFO entry
}
static void Pump() {
    for (unsigned budget=0;!transmitted.empty() || !received.empty();++budget) {
        assert(budget<10000);
        while (!transmitted.empty()) {
            const auto f=transmitted.front(); transmitted.pop_front();
            peer_feed(f.id,f.bytes.data(),f.bytes.size()); fakeCan.TSR|=CAN_TSR_TME0<<f.mailbox;
        }
        while (!received.empty()) { const auto f=received.front(); received.pop_front(); DeliverOne(f); }
        SyncRegisters(); Spin();
    }
}
static void Step(unsigned milliseconds=1,bool spin=true) {
    for (unsigned i=0;i<milliseconds;++i) {
        hostTicks+=1000; SyncRegisters();
        peer_tick(hostTicks);
        if (sampleOn && !suppressSamples && hostTicks>=nextSample) { peer_sample(sampleCount,55927); nextSample=hostTicks+samplePeriodUs; }
        if (spin) { Spin(); Pump(); }
    }
}
static std::string Hex(const uint8_t* bytes,size_t count) {
    std::string result; static const char digits[]="0123456789abcdef";
    for (size_t i=0;i<count;++i) { result+=digits[bytes[i]>>4]; result+=digits[bytes[i]&15]; } return result;
}
static void ConfigureModel(const uint8_t uuid[6],unsigned profile) {
    auto b=CartographerTest::Model(); std::memcpy(b.data()+16,uuid,6);
    std::memcpy(b.data()+24,Profiles[profile].identifyHash,32);
    CartographerTest::Number(b,104,1.0/3000500); CartographerTest::Number(b,112,1.0/2999500);
    for (unsigned i=0;i<10;++i) CartographerTest::Number(b,120+8*i,i==0?1:i==1?.5:0);
    CartographerTest::Number(b,200,20.5); CartographerTest::Number(b,208,20); CartographerTest::Number(b,216,21);
    CartographerTest::Number(b,224,0); CartographerTest::Number(b,232,.5); CartographerTest::Number(b,240,1.5);
    CartographerTest::Seal(b); fakeFileBytes.assign(b.begin(),b.end());
    assert(Call({{'S',"2"},{'P',"emulated.cart"},{'U',Hex(uuid,6)},{'B',"pei"}})==GCodeResult::ok);
}
static GCodeResult Prepare(bool touch) {
    StringRef output(reply,2048); return PrepareProbe(touch,.8f,.1f,output);
}
static void WaitPrepared(bool touch) {
    GCodeResult result=GCodeResult::notFinished;
    for (unsigned i=0;i<90 && result==GCodeResult::notFinished;++i) { result=Prepare(touch); if (result==GCodeResult::notFinished) Step(); }
    assert(result==GCodeResult::ok);
}
static void Check(bool condition,const char* description) {
    if (!condition) { std::fprintf(stderr,"Full-stack assertion failed: %s\nreply: %s\n",description,reply.c_str()); std::abort(); }
}
static void CheckAbortHelpersAfterCleanup() {
    std::mutex gate; std::condition_variable condition; bool locked=false, release=false;
    auto holder=std::async(std::launch::async,[&] {
        MutexLocker runtime(*fakeCreatedMutex);
        std::unique_lock<std::mutex> lock(gate); locked=true; condition.notify_all();
        condition.wait(lock,[&] { return release; });
    });
    { std::unique_lock<std::mutex> lock(gate); Check(condition.wait_for(lock,std::chrono::seconds(2),[&] { return locked; }),"new task owns mutex after emergency cleanup"); }
    auto abortHelpers=std::async(std::launch::async,[] { EndScan(); DisarmProbe(); });
    const bool returned=abortHelpers.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
    { std::lock_guard<std::mutex> lock(gate); release=true; } condition.notify_all(); holder.get(); abortHelpers.get();
    Check(returned,"AbortPrint helpers do not wait even after emergency pending flag cleared");
}
int main(int argc,char** argv) {
    const std::string scenario=argc>1?argv[1]:"scan";
    const unsigned profile=argc>2?std::stoul(argv[2]):0;
    samplePeriodUs=Profiles[profile].periodUs;
    uint8_t uuid[6]; peer_uuid(uuid);
    fakeCan.TSR=FreeMailboxes; SyncRegisters(); Init();
    Check(Call({{'S',"4"}})==GCodeResult::ok,"start discovery"); Step(2100);
    Check(Call({{'S',"5"}})==GCodeResult::ok && reply.find(Hex(uuid,6))!=std::string::npos,"discover actual MCU UUID");
    Check(reply.find("cartographer-v3-discovery-v1")!=std::string::npos && reply.find("complete")!=std::string::npos,"discovery JSON complete");
    Check(peer_home_calls()==0,"discovery does not arm probe");
    Check(Call({{'S',"1"},{'U',Hex(uuid,6)},{'H',"1"}})==GCodeResult::ok,"start opted-in trigger session");
    Check(Call({{'S',"8"}})==GCodeResult::notFinished,"readiness is asynchronous");
    Step(2100);
    Check(Call({{'S',"8"}})==GCodeResult::ok,"raw session ready");
    ConfigureModel(uuid,profile); Check(ModelReady(),"model becomes ready through verified loading");
    float height=0; Check(ReadHeight(height) && height>.99 && height<1.01,"double raw frequency maps to expected model height");
    if (scenario=="capture") Step(80); // exported host clock must survive the 32-bit timer wrap
    Check(Call({{'S',"7"},{'Z',"1"}})==GCodeResult::ok,"actual capture command");
    Check(reply.find("cartographer-v3-sample-v1")!=std::string::npos && reply.find("frequency_bits")!=std::string::npos && reply.find("temperature_bits")!=std::string::npos,"capture includes exact binary64 fields");
    if (scenario=="capture") { std::puts(reply.c_str()); Exit(); return 0; }
    if (scenario=="emergency-blocked-io") {
        fakeBlockFileRead.store(true); fakeFileReadEntered.store(false); fakeContinueFileRead.store(false);
        auto loading=std::async(std::launch::async,[&] {
            GCodeBuffer buffer({{'S',"2"},{'P',"blocked.cart"},{'U',Hex(uuid,6)},{'B',"pei"}});
            std::string workerReply; StringRef output(workerReply,2048); return Configure(buffer,output);
        });
        { std::unique_lock<std::mutex> lock(fakeFileReadMutex);
          Check(fakeFileReadCondition.wait_for(lock,std::chrono::seconds(2),[] { return fakeFileReadEntered.load(); }),"model loader holds runtime mutex during blocked SD read"); }
        auto emergency=std::async(std::launch::async,[] { EmergencyStop(); });
        const bool returned=emergency.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
        if (!returned) { fakeContinueFileRead.store(true); fakeFileReadCondition.notify_all(); emergency.wait(); loading.wait(); }
        Check(returned,"EmergencyStop returns while model SD read remains blocked"); emergency.get();
        Check(!fakeContinueFileRead.load() && ProbeFailed() && !ProbeTriggered(),"emergency publishes immediate probe failure before deferred cleanup");
        Check(!fakeIrq && (fakeRcc.APB1ENR&RCC_APB1ENR_CAN1EN)==0 && (fakeGpio.MODER&15)==0,"emergency abort disables CAN/IRQ without runtime mutex");
        auto abortHelpers=std::async(std::launch::async,[] { EndScan(); DisarmProbe(); });
        const bool helpersReturned=abortHelpers.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
        auto restart=std::async(std::launch::async,[&] {
            GCodeBuffer buffer({{'S',"1"},{'U',Hex(uuid,6)},{'H',"1"}});
            std::string workerReply; StringRef output(workerReply,2048); return Configure(buffer,output);
        });
        const bool rejectedPromptly=restart.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
        fakeContinueFileRead.store(true); fakeFileReadCondition.notify_all();
        Check(rejectedPromptly && restart.get()==GCodeResult::error,"pending emergency promptly rejects concurrent session restart");
        abortHelpers.get(); Check(helpersReturned,"AbortPrint helpers never wait on blocked SD read after emergency");
        Check(loading.get()==GCodeResult::error,"in-flight calibration file load fails closed after emergency");
        fakeBlockFileRead.store(false); Spin();
        Check(fakeOwnedPins==0 && ProbeFailed() && !ModelReady(),"deferred cleanup releases pins and invalidates session");
        Check(Call({{'S',"8"}})==GCodeResult::error,"readiness cannot revive stopped emergency session");
        Check(!ArmProbe(false,.8f,.1f),"operation cannot arm after emergency cleanup");
        CheckAbortHelpersAfterCleanup();
    } else if (scenario=="transactional") {
        Check(Call({{'S',"6"},{'T',"1"},{'E',"0.8"},{'V',"0.1"},{'D',"1000"},{'R',"2500"}})==GCodeResult::ok && TouchMode(),"configure model-specific Touch settings");
        const auto original=fakeFileBytes; fakeFileBytes[120]^=1;
        Check(Call({{'S',"2"},{'P',"bad.cart"},{'U',Hex(uuid,6)},{'B',"pei"}})==GCodeResult::error && ModelReady(),"bad CRC replacement preserves model");
        Check(TouchMode() && TouchThreshold()==2500,"rejected replacement retains verified operating settings");
        fakeFileBytes=original;
        std::string wrong=Hex(uuid,6); wrong.back()=wrong.back()=='0'?'1':'0';
        Check(Call({{'S',"2"},{'P',"bad.cart"},{'U',wrong},{'B',"pei"}})==GCodeResult::error && ModelReady(),"UUID mismatch preserves model");
        Check(Call({{'S',"2"},{'P',"good.cart"},{'U',Hex(uuid,6)},{'B',"pei"}})==GCodeResult::ok && ModelReady(),"valid replacement commits model");
        Check(!TouchMode() && TouchThreshold()==0,"successful model load invalidates previous Touch threshold and datum");
        Check(PrepareProbe(false,.8f,.1f,StringRef(reply,2048))==GCodeResult::error && peer_home_calls()==0,"replacement requires a new explicit S6 before arming");
        Check(Call({{'L',"uk"}})==GCodeResult::ok && IsUkrainian() && ModelReady() && fakeOwnedPins==2,"Ukrainian locale preserves live CAN/model");
        Check(Call({{'S',"6"}})==GCodeResult::error && IsUkrainian(),"new action validation in Ukrainian");
        Check(Call({{'L',"en"}})==GCodeResult::ok && !IsUkrainian() && ModelReady() && fakeOwnedPins==2,"English locale preserves live CAN/model");
        Check(Call({{'S',"6"}})==GCodeResult::error,"new action validation in English");
    } else if (scenario=="scan-height") {
        Check(BeginScan(),"begin read-only scan"); Step(20);
        Check(ReadHeightAtState(static_cast<uint32_t>(hostTicks-samplePeriodUs-500),height)==GCodeResult::ok,"time-aligned buffered height");
        const auto future=static_cast<uint32_t>(hostTicks+samplePeriodUs);
        Check(ReadHeightAtState(future,height)==GCodeResult::notFinished,"scan waits for future sensor sample");
        Step(samplePeriodUs/1000+2); Check(ReadHeightAtState(future,height)==GCodeResult::ok,"queued scan sample resolves at original timestamp");
        EndScan(); Check(peer_home_calls()==0,"scan does not command remote homing");
    } else {
        const bool touch=scenario=="touch";
        Check(Call({{'S',"6"},{'T',touch?"1":"0"},{'E',"0.8"},{'V',"0.1"},{'D',"1000"},{'R',"2500"}})==GCodeResult::ok,"explicit operation settings");
        WaitPrepared(touch); Check(peer_home_calls()==1,"prepare issued remote home once");
        Check(peer_home_method()==(touch?1u:0u) && peer_home_threshold()==2500,"actual MCU parser received the selected method and exact Touch threshold");
        Check(ArmProbe(touch,.8f,.1f),"arm local ISR flags after remote readiness");
        Check(!ProbeTriggered() && !ProbeFailed(),"no successful hit before remote event");
        if (scenario=="emergency-armed-lock") {
            std::mutex gate; std::condition_variable condition; bool locked=false, release=false;
            auto holder=std::async(std::launch::async,[&] {
                MutexLocker runtime(*fakeCreatedMutex);
                std::unique_lock<std::mutex> lock(gate); locked=true; condition.notify_all();
                condition.wait(lock,[&] { return release; });
            });
            { std::unique_lock<std::mutex> lock(gate); Check(condition.wait_for(lock,std::chrono::seconds(2),[&] { return locked; }),"other task holds runtime mutex during active probe"); }
            auto emergency=std::async(std::launch::async,[] { EmergencyStop(); });
            const bool returned=emergency.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
            if (!returned) { { std::lock_guard<std::mutex> lock(gate); release=true; } condition.notify_all(); holder.wait(); emergency.wait(); }
            Check(returned,"armed EmergencyStop never waits for runtime mutex"); emergency.get();
            Check(ProbeFailed() && !ProbeTriggered() && !fakeIrq && (fakeRcc.APB1ENR&RCC_APB1ENR_CAN1EN)==0,"armed probe fails immediately with peripheral aborted");
            { std::lock_guard<std::mutex> lock(gate); release=true; } condition.notify_all(); holder.get(); Spin();
            Check(fakeOwnedPins==0 && ProbeFailed(),"pending armed emergency finishes deferred cleanup");
            CheckAbortHelpersAfterCleanup();
        } else if (scenario=="stale-without-isr") {
            // No ISR getter runs before the valid late hit reaches cooperative Spin.
            // Supervision itself must latch the previously expired local lease first.
            hostTicks+=25000; SyncRegisters(); peer_tick(hostTicks); peer_sample(sampleCount,55927); peer_hit(); Pump();
            Check(ProbeFailed() && !ProbeTriggered(),"queued genuine hit cannot revive expired lease without an earlier ISR read");
        } else if (scenario=="stale-isr") {
            // No Spin or remote traffic: step ISR must observe its own raw deadline.
            hostTicks+=70000; SyncRegisters(); Check(ProbeFailed() && !ProbeTriggered(),"ISR watchdog faults without cooperative Spin");
            peer_tick(hostTicks); peer_sample(sampleCount,55927); peer_hit(); Pump();
            Check(ProbeFailed() && !ProbeTriggered(),"late valid hit cannot revive an ISR-failed operation");
        } else if (scenario=="invalid-sample") {
            sampleCount=0x0fffffff; Step(samplePeriodUs/1000+1); Check(ProbeFailed() && !ProbeTriggered(),"invalid raw sample faults active probe");
        } else if (scenario=="bus-off") {
            fakeCan.ESR=CAN_ESR_BOFF; Step(); Check(ProbeFailed() && !ProbeTriggered(),"bus-off cannot succeed as trigger");
        } else if (scenario=="overflow") {
            const Frame noise={ResponseCanId,{0},0}; for (unsigned i=0;i<70;++i) DeliverOne(noise);
            Step(); Check(ProbeFailed() && !ProbeTriggered(),"bounded RX overflow faults active probe");
        } else if (scenario=="remote-timeout") {
            // Advance real MCU timer beyond expiry without host polling/refresh.
            hostTicks+=110000; SyncRegisters(); peer_tick(hostTicks); Pump();
            Check(ProbeFailed() && !ProbeTriggered(),"MCU CommsTimeout is failure, never hit");
        } else if (scenario=="report-loss") {
            for (unsigned i=0;i<70;++i) {
                hostTicks+=1000; SyncRegisters(); suppressReports=true; peer_tick(hostTicks); suppressReports=false;
                peer_sample(sampleCount,55927); Spin(); Pump();
            }
            Check(ProbeFailed() && !ProbeTriggered(),"lost trsync reports fault despite fresh raw samples");
        } else if (scenario=="bad-oid") {
            peer_report(1,0,1,static_cast<uint32_t>(hostTicks*48+0xfffffff0ull)); Pump();
            Check(ProbeFailed() && !ProbeTriggered(),"foreign trsync OID rejected");
        } else {
            Step(50); peer_hit(); Pump();
            Check(ProbeTriggered() && !ProbeFailed(),"only actual EndstopHit sets successful ISR flag");
            DisarmProbe(); Step(20); Check(!peer_home_active(),"disarm stops remote home handler");
        }
    }
    Diagnostics(0); Check(fakeDiagnostics.find("cartographer-v3")!=std::string::npos,"registry diagnostics from production facade");
    EmergencyStop(); Check(fakeOwnedPins==0 && !fakeIrq && (fakeRcc.APB1ENR&RCC_APB1ENR_CAN1EN)==0,"emergency cleanup releases pins/CAN");
    Check(txFrames>50 && rxFrames>200,"complete CAN protocol traversed production driver and IRQ");
    std::fprintf(stderr,"Cartographer full stack: %s profile=%u passed (%u driver TX, %u IRQ RX)\n",scenario.c_str(),profile,txFrames,rxFrames);
}
