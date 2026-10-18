#include <RepRapFirmware.h>
#include <cassert>
#include <cstddef>
#include <cstdio>
struct TaskCriticalSectionLocker { TaskCriticalSectionLocker() { } ~TaskCriticalSectionLocker() { } };
struct TestProbe {
    GCodeResult result=GCodeResult::ok; float height=0.25f; uint32_t lastTime=0;
    GCodeResult GetCalibratedReadingAt(uint32_t time,float& value) { lastTime=time; value=height; return result; }
};
struct TestHeightMap {
    unsigned writes=0; float height=0; size_t x=0,y=0;
    void SetGridHeight(size_t px,size_t py,float h) { ++writes;x=px;y=py;height=h; }
};
struct TestMove {
    bool pending=true; uint32_t generation=1; TestHeightMap map;
    bool ScanningProbeReadingsPending() const { return pending; }
    uint32_t ScanningProbeGeneration() const { return generation; }
    TestHeightMap& AccessHeightMap() { return map; }
};
struct TestRepRap { TestMove move; TestMove& GetMove() { return move; } } reprap;
struct TestPlatform { TestProbe probe; TestProbe* GetZProbeOrDefault(unsigned) { return &probe; } };
class GCodes {
public:
    TestPlatform platform; unsigned currentZProbeNumber=0;
    GCodeResult scanningResult=GCodeResult::ok;
    size_t gridAxis0Index=0,gridAxis1Index=0,lastAxis0Index=3;
    bool TakeScanningProbeReading(uint32_t rawTime,uint32_t generation) noexcept;
};
