#include "CartographerZProbe.h"
#include "CartographerV3.h"
#include <GCodes/GCodeBuffer/GCodeBuffer.h>
#include <cassert>
#include <cmath>
#include <cstdio>
namespace RrfModules::CartographerV3 {
static bool fresh=true, touching=false, ukrainian=false, failed=false, triggered=false, prepared=false, scanning=false;
static unsigned arms=0, disarms=0, scans=0;
static float absolute=2.25f, lastHeight=0, lastSpeed=0;
static uint32_t queriedTime=0;
static GCodeResult preparation=GCodeResult::notFinished, atResult=GCodeResult::ok;
bool ReadHeight(float& value) noexcept { if(!fresh)return false; value=absolute; return true; }
GCodeResult ReadHeightAtState(uint32_t time,float& value) noexcept { queriedTime=time; value=absolute; return fresh?atResult:GCodeResult::error; }
bool BeginScan() noexcept { ++scans; if(scanning)return false; scanning=fresh; return scanning; }
void EndScan() noexcept { scanning=false; }
bool ModelReady() noexcept { return fresh; }
GCodeResult PrepareProbe(bool touch,float height,float speed,const StringRef&) {
    assert(touch==touching); lastHeight=height; lastSpeed=speed; prepared=preparation==GCodeResult::ok; return preparation;
}
bool ArmProbe(bool touch,float height,float speed) noexcept {
    assert(touch==touching && height==lastHeight && speed==lastSpeed); ++arms; return prepared;
}
void DisarmProbe() noexcept { ++disarms; prepared=false; }
bool ProbeTriggered() noexcept { return triggered; }
bool ProbeFailed() noexcept { return failed; }
bool TouchMode() noexcept { return touching; }
float TouchTriggerHeight() noexcept { return 0.05f; }
float TouchProbeSpeed() noexcept { return 0.5f; }
float TouchThreshold() noexcept { return 9000.0f; }
bool IsUkrainian() noexcept { return ukrainian; }
}
namespace C=RrfModules::CartographerV3;
int main() {
    std::string text; StringRef reply(text,1024);
    CartographerZProbe probe(2);
    assert(probe.UsesExternalModel() && probe.IsCalibrated());
    assert(probe.PrepareForProbing(reply)==GCodeResult::notFinished && C::arms==0);
    C::preparation=GCodeResult::ok;
    assert(probe.PrepareForProbing(reply)==GCodeResult::ok && C::lastHeight==2.0f && C::lastSpeed==2.0f);
    assert(!probe.StartHoming(ConvertSpeedFromMmPerSec(1.0f)));
    assert(!probe.StartHoming(ConvertSpeedFromMmPerSec(-3.0f)));
    assert(probe.StartHoming(ConvertSpeedFromMmPerSec(-2.0f)) && C::arms==1);
    assert(probe.CheckTriggered().GetAction()==EndstopHitAction::none);
    C::triggered=true;
    auto hit=probe.CheckTriggered(); assert(hit.GetAction()==EndstopHitAction::stopAll && hit.isZProbe && !hit.probeError);
    C::triggered=false; C::failed=true;
    hit=probe.CheckTriggered(); assert(hit.GetAction()==EndstopHitAction::stopAll && hit.probeError);
    probe.EndProbeMove(); assert(probe.HasProbeError()); // disarm must retain fault for completion handling
    C::failed=false;
    float value=0;
    assert(probe.GetCalibratedReading(value)==GCodeResult::ok && value==0.25f);
    C::atResult=GCodeResult::notFinished;
    assert(probe.GetCalibratedReadingAt(0xfffffff0u,value)==GCodeResult::notFinished && C::queriedTime==0xfffffff0u);
    C::atResult=GCodeResult::ok;
    assert(probe.GetCalibratedReadingAt(10,value)==GCodeResult::ok && value==0.25f && C::queriedTime==10);
    assert(probe.PrepareForScanning(reply)==GCodeResult::ok && C::scans==1);
    assert(probe.PrepareForScanning(reply)==GCodeResult::ok && C::scans==1); // retry retains one operation reservation
    assert(probe.SetProbing(true));
    probe.SetProbing(false); assert(C::scanning); // row completion cannot release the model for a replacement
    probe.EndScanning(); assert(!C::scanning);
    C::fresh=false; assert(!probe.IsCalibrated());
    assert(probe.GetCalibratedReading(value)==GCodeResult::error && std::isnan(probe.GetLatestHeight()));
    assert(probe.PrepareForScanning(reply)==GCodeResult::error && text.find("unavailable")!=std::string::npos);
    C::ukrainian=true; assert(probe.PrepareForScanning(reply)==GCodeResult::error && text.find("недоступна")!=std::string::npos);
    C::fresh=true; C::touching=true;
    assert(probe.GetTouchModeActive() && probe.GetTouchModeHeight()==0.05f && probe.GetTouchModeSpeed()==30.0f && probe.GetTouchModeThreshold()==9000.0f);
    assert(probe.GetActiveModeTriggerHeight()==0.05f && probe.GetProbingSpeed(0)==ConvertSpeedFromMmPerSec(0.5f));
    assert(probe.PrepareForScanning(reply)==GCodeResult::error);
    assert(probe.PrepareForProbing(reply)==GCodeResult::ok && C::lastHeight==0.05f && C::lastSpeed==0.5f);
    bool seen=false; GCodeBuffer bad({{'P',"12"},{'C',"wrong"}});
    assert(probe.Configure(bad,reply,seen)==GCodeResult::error && text.find("Вкажіть")!=std::string::npos);
    GCodeBuffer good({{'P',"12"},{'C',"cartographer-v3"}});
    assert(probe.Configure(good,reply,seen)==GCodeResult::ok && seen);
    std::puts("Cartographer RRF ZProbe adapter: async preparation, mode/speed, scan reservation, timestamp reads, en/uk and fail-closed trigger passed");
}
