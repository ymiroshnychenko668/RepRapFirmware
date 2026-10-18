#include <RepRapFirmware.h>
#include "ProbeMessages.h"
#include <cassert>
#include <cstdio>
#define _ecv_from
#define _ecv_null
constexpr unsigned Z_AXIS=2;
namespace RrfModules::CartographerV3 { bool IsUkrainian() noexcept { return false; } }
enum class EndstopHitAction { none, stopDriver, stopAxis, stopAll };
struct EndstopHitDetails {
    EndstopHitAction action=EndstopHitAction::none;
    unsigned axis=2; bool isZProbe=false,probeError=false,setAxisHigh=false,setAxisLow=false;
    struct { unsigned localDriver=0; } driver;
    EndstopHitAction GetAction() const { return action; }
};
struct EndstopOrZProbe {
    EndstopOrZProbe* next=nullptr; EndstopHitDetails hit;
    EndstopOrZProbe* GetNext() { return next; }
    void SetNext(EndstopOrZProbe* value) { next=value; }
    EndstopHitDetails CheckTriggered() { return hit; }
    bool Acknowledge(EndstopHitDetails) { return true; }
};
struct EndstopsManager {
    EndstopOrZProbe* activeEndstops=nullptr; bool isHomingMove=false,probeMoveFailed=false;
    unsigned finished=0;
    EndstopHitDetails CheckEndstops() noexcept;
    bool AnyEndstopsActive() const { return activeEndstops!=nullptr; }
    void FinishProbeMove() { ++finished; }
    bool ProbeMoveFailed() const { return probeMoveFailed; }
};
struct TestPlatform { EndstopsManager endstops; EndstopsManager& GetEndstops() { return endstops; } } platform;
enum class GCodeState { normal,checkError };
class GCodeBuffer {
public:
    GCodeState state=GCodeState::normal; std::string error;
    GCodeBuffer& LatestMachineState() { return *this; }
    void SetError(const char* value) { error=value; }
    void SetState(GCodeState value) { state=value; }
};
struct GCodes {
    TestPlatform& platform=::platform;
    unsigned probeTriggers=0,axisTriggers=0,retractions=0;
    bool ready=false,zHomed=true;
    bool LockCurrentMovementSystemAndWaitForStandstill(GCodeBuffer&) { return ready; }
    bool WaitForEndstopOrProbingMoveToFinish(GCodeBuffer&) noexcept;
    void MoveStoppedByZProbe() { ++probeTriggers; }
    void RecordEndstopTriggered(unsigned,int) { ++axisTriggers; }
    void SetAxisNotHomed(unsigned axis) { assert(axis==Z_AXIS);zHomed=false; }
    void RetractZProbe(GCodeBuffer&) { ++retractions; }
};
struct TestHeat { bool suspended=true; void SuspendHeaters(bool value) { suspended=value; } };
struct TestRepRap {
    GCodes gcodes; TestHeat heat;
    GCodes& GetGCodes() { return gcodes; }
    TestPlatform& GetPlatform() { return ::platform; }
    TestHeat& GetHeat() { return heat; }
} reprap;
struct Kinematics { int GetHomingMode() const { return 0; } } kinematics;
namespace StepPins { unsigned CalcDriverBitmap(unsigned driver) { return 1u<<driver; } }
struct Move {
    Kinematics* kinematics=&::kinematics;
    struct { unsigned driversCurrentlyUsed=3,driverEndstopsTriggeredAtStart=0; } dms[3];
    unsigned stopAll=0,stopAxis=0,wakes=0;
    void CheckEndstops(bool executingMove) noexcept;
    void StopAllDrivers(bool) { ++stopAll;for(auto& drive:dms)drive.driversCurrentlyUsed=0; }
    void StopAxisOrExtruder(bool,unsigned) { ++stopAxis; }
    void WakeMoveTask() { ++wakes; }
};
