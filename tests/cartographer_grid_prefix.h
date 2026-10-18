#include <RepRapFirmware.h>
#include "ProbeMessages.h"
#include <map>
#include <cassert>
#include <cmath>
#include <cinttypes>
#include <cstdio>
constexpr size_t MaxFilenameLength=120;
constexpr int WarningMessage=1,LogWarn=2;
namespace RrfModules::CartographerV3 { bool IsUkrainian() noexcept { return false; } }
class ZProbe { public: bool external=false;bool UsesExternalModel() const { return external; } };
struct Deviation { float GetMean() const { return 0; }float GetDeviationFromMean() const { return 0; } };
struct TestMap {
    unsigned clears=0,extrapolations=0;
    void ClearGridHeights() { ++clears; }
    void ExtrapolateMissing() { ++extrapolations; }
    uint32_t GetStatistics(Deviation&,float& minimum,float& maximum) { minimum=maximum=0;return 4; }
};
struct TestMove {
    TestMap map;bool active=false;
    TestMap& AccessHeightMap() { return map; }
    void SetLatestMeshDeviation(const Deviation&) { }
    void UseMesh(bool value) { active=value; }
};
struct TestPlatform {
    ZProbe probe;unsigned ended=0;
    ZProbe* GetZProbeOrDefault(unsigned) { return &probe; }
    TestPlatform& GetEndstops() { return *this; }
    void EndScanning() { ++ended; }
    void Message(int,const char*) { }
    void MessageF(int,const char*,const char*) { }
} platform;
struct TestRepRap { TestMove move;TestMove& GetMove() { return move; }TestPlatform& GetPlatform() { return platform; } } reprap;
enum class GCodeState { normal,gridProbing7 };
class GCodeBuffer {
    std::map<char,std::string> parameters;char current=0;
public:
    GCodeState state=GCodeState::gridProbing7;std::string error;
    GCodeBuffer(std::initializer_list<std::pair<const char,std::string>> init={}) : parameters(init) { }
    bool Seen(char key) { current=key;return parameters.count(key)!=0; }
    void GetQuotedString(const StringRef& value) { value.copy(parameters.at(current).c_str()); }
    GCodeBuffer& LatestMachineState() { return *this; }
    void SetError(const char* value) { error=value; }
    void SetState(GCodeState value) { state=value; }
};
class GCodes {
public:
    TestPlatform& platform=::platform;
    static constexpr const char* DefaultHeightMapFile="heightmap.csv";
    String<MaxFilenameLength> cartographerHeightMapFile;
    unsigned currentZProbeNumber=0,saves=0;bool saveFailed=false;
    GCodeResult scanningResult=GCodeResult::ok,result=GCodeResult::ok;
    std::string savedName;
    GCodeResult SelectProbeGridFilename(GCodeBuffer&,const ZProbe&,const StringRef&);
    const char* ProbeGridOutputFile() const noexcept;
    bool TrySaveHeightMap(const char* name,const StringRef&) { ++saves;savedName=name;return saveFailed; }
    void FinishGrid(GCodeBuffer&,const StringRef&);
};
