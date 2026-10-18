#include "CartographerV3.h"
#include "CanTransport.h"
#include "Messages.h"
#include "CalibrationModel.h"
#include <CoreIO.h>
#include <GCodes/GCodeBuffer/GCodeBuffer.h>
#include <Hardware/IoPorts.h>
#include <Movement/StepTimer.h>
#include <Platform/Platform.h>
#include <Platform/RepRap.h>
#include <Storage/FileStore.h>
#include <RTOSIface/RTOSIface.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cinttypes>

namespace RrfModules::CartographerV3 {
namespace {
CanTransport can;
Client client(can);
CalibrationModel model;
char selectedSurface[32] = {};
ModelError lastModelLoad = ModelError::None;
Mutex runtimeMutex;
std::atomic<bool> mutexCreated{false}, emergencyPending{false}, emergencyLatched{false}, ukrainian{false};
std::atomic<uint32_t> emergencyGeneration{0};
bool scanActive=false, settingsDefined=false, settingsTouch=false, preparing=false;
float settingsHeight=0, settingsSpeed=0, preparedHeight=0, preparedSpeed=0;
uint32_t settingsDuration=0, settingsTouchThreshold=0;
bool preparedTouch=false;
uint64_t connectedAt=0, operationDeadline=0;
double armedFrequency=0, armedTemperature=0;
enum : uint32_t { LocalActive=1, LocalTriggered=2, LocalFailed=4 };
std::atomic<uint32_t> localFlags{0}, localDeadline{0};
static_assert(std::atomic<uint32_t>::is_always_lock_free,"Probe ISR requires lock-free 32-bit atomics");
struct DiscoveryNode { uint8_t uuid[6], application; } discoveryNodes[8];
unsigned discoveryCount=0;
bool discovering=false, discoverySent=false, discoveryComplete=false, discoveryFault=false, discoveryOverflow=false;
uint32_t discoveryStarted=0;
IoPort receivePin, transmitPin;
bool enabled = false, connecting = false, triggersWanted=false;
uint8_t selectedUuid[6] = {};
uint32_t previousClock = 0;
uint64_t extendedClock = 0;
uint64_t Now() noexcept {
    const uint32_t ticks = StepTimer::GetTimerTicks();
    extendedClock += static_cast<uint32_t>(ticks - previousClock); previousClock = ticks;
    return extendedClock;
}
void Release() noexcept { can.Stop(); receivePin.Release(); transmitPin.Release(); enabled = connecting = false; }
void CompleteEmergency() noexcept {
    const uint32_t generation=emergencyGeneration.load(std::memory_order_acquire);
    client.Stop(); scanActive=preparing=discovering=false;
    localFlags.store(LocalFailed,std::memory_order_release); Release();
    const auto flags=IrqSave();
    if (emergencyGeneration.load(std::memory_order_acquire)==generation) emergencyPending.store(false,std::memory_order_release);
    IrqRestore(flags);
}
bool Busy() noexcept { return scanActive || preparing || (localFlags.load(std::memory_order_acquire)&LocalActive)!=0; }
void FailOperation() noexcept { localFlags.fetch_or(LocalFailed,std::memory_order_release); }
ModelError CurrentHeight(uint64_t now, double& height, double* corrected=nullptr) noexcept {
    height=0;
    if (emergencyLatched.load(std::memory_order_acquire)) return ModelError::Stale;
    if (!model.Loaded()) return ModelError::Missing;
    if (!enabled || connecting || client.GetProfile()==nullptr || !client.HasFreshSample(now)) return ModelError::Stale;
    const Sample& sample=client.LastSample();
    const ModelError result=model.Evaluate(sample.frequency,sample.temperature,selectedUuid,client.GetProfile()->identifyHash,selectedSurface,height,corrected);
    return emergencyLatched.load(std::memory_order_acquire)?ModelError::Stale:result;
}
void Status(const StringRef& reply, bool detailed = true) noexcept {
    const uint64_t now = Now();
    const Sample& sample = client.LastSample();
    double height=0, corrected=0;
    const ModelError heightError=CurrentHeight(now,height,&corrected);
    const char* state=connecting ? "can_starting" : client.GetState()==State::Ready && heightError!=ModelError::None ? "uncalibrated" : Client::StateName(client.GetState());
    reply.printf("cartographer-v3 state=%s fault=%s fresh=%u uuid=%02x%02x%02x%02x%02x%02x profile=\"%s\" height_valid=%u height_status=%s",
        state, Client::FaultName(client.GetFault()),
        client.HasFreshSample(now) ? 1u : 0u, selectedUuid[0], selectedUuid[1], selectedUuid[2], selectedUuid[3], selectedUuid[4], selectedUuid[5],
        !connecting && client.GetProfile()!=nullptr ? client.GetProfile()->name : "none", heightError==ModelError::None ? 1u:0u, CalibrationModel::ErrorName(heightError));
    if (!detailed) return;
    reply.catf("\nheight_mm=%.6f compensated_hz=%.6f surface=\"%s\" model_loaded=%u model_load_error=%s raw_state=%s",
        height,corrected,selectedSurface,model.Loaded()?1u:0u,CalibrationModel::ErrorName(lastModelLoad),Client::StateName(client.GetState()));
    reply.catf("\nraw=%" PRIu32 " adc_sum=%" PRIu32 " frequency=%.3f temperature=%.3f valid=%u age_us=%" PRIu32,
        sample.count, sample.adcSum, static_cast<double>(sample.frequency), static_cast<double>(sample.temperature),
        sample.valid ? 1u : 0u, client.SampleAgeUs(now));
    reply.catf("\nsamples=%" PRIu32 " invalid=%" PRIu32 " overwritten=%" PRIu32 " retries=%" PRIu32 " timeouts=%" PRIu32 " crc_errors=%" PRIu32 " malformed=%" PRIu32 " unknown_messages=%" PRIu32,
        client.samples, client.invalidSamples, client.overwritten, client.retries, client.timeouts, client.BadCrc(), client.Malformed(), client.unknownMessages);
    reply.catf("\nclock_samples=%" PRIu32 " clock_ready=%u rtt_us=%" PRIu32 " uncertainty_us=%" PRIu32 " rx_frames=%" PRIu32 " rx_overflow=%" PRIu32 " can_esr=%08" PRIx32,
        client.Clock().Count(), client.Clock().Ready(now) ? 1u : 0u, client.Clock().RttUs(), client.Clock().UncertaintyUs(now),
        can.RxCount(), can.Overflow(), can.Errors());
    reply.catf("\nsample_clock=%" PRIu64 " sample_host_ticks=%" PRIu64 " host_rate=%" PRIu32 " base_count=%" PRIu32 " base_adc=%u",
        sample.mcuClock, sample.hostTime, StepTimer::GetTickRate(), client.BaseCount(), client.BaseAdc());
    reply.catf("\ntrigger_configured=%u trigger_state=%u trigger_reason=%u probe_flags=%" PRIu32 " touch=%u scan_active=%u",
        client.TriggerConfigured()?1u:0u,static_cast<unsigned>(client.GetTriggerState()),client.TriggerReason(),localFlags.load(std::memory_order_acquire),settingsTouch?1u:0u,scanActive?1u:0u);
}
int Hex(char value) noexcept {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}
bool Uuid(GCodeBuffer& gb, uint8_t uuid[6]) THROWS(GCodeException) {
    String<20> requested;
    if (gb.Seen('U')) gb.GetQuotedString(requested.GetRef());
    if (requested.strlen()!=12) return false;
    bool allZero=true, allFF=true;
    for (unsigned i=0;i<6;++i) {
        const int high=Hex(requested.c_str()[2*i]), low=Hex(requested.c_str()[2*i+1]);
        if (high<0 || low<0) return false;
        uuid[i]=(high<<4)|low; allZero=allZero && uuid[i]==0; allFF=allFF && uuid[i]==255;
    }
    return !allZero && !allFF;
}
GCodeResult LoadModel(GCodeBuffer& gb, const StringRef& reply) THROWS(GCodeException) {
    if (Busy()) { reply.copy(Text(Message::Busy,ukrainian)); return GCodeResult::error; }
    uint8_t uuid[6];
    if (!Uuid(gb,uuid)) { reply.copy(Text(Message::Uuid,ukrainian)); return GCodeResult::error; }
    String<120> filename; String<32> surface;
    if (gb.Seen('P')) gb.GetQuotedString(filename.GetRef());
    if (gb.Seen('B')) gb.GetQuotedString(surface.GetRef());
    if (filename.strlen()==0 || surface.strlen()==0) { reply.copy(Text(Message::ModelParameters,ukrainian)); return GCodeResult::error; }
    const uint32_t generation=emergencyGeneration.load(std::memory_order_acquire);
    FileStore* file=reprap.GetPlatform().OpenFile("",filename.c_str(),OpenMode::read);
    if (file==nullptr) { reply.copy(Text(Message::ModelFile,ukrainian)); return GCodeResult::error; }
    uint8_t bytes[CalibrationFileSize];
    const bool read=file->Length()==sizeof(bytes) && file->Read(bytes,sizeof(bytes))==static_cast<int>(sizeof(bytes));
    const bool closed=file->Close();
    if (!read || !closed) { reply.copy(Text(Message::ModelFile,ukrainian)); return GCodeResult::error; }
    CalibrationModel candidate;
    lastModelLoad=candidate.Load(bytes,sizeof(bytes));
    if (lastModelLoad==ModelError::None) {
        const auto& p=candidate.Parameters();
        bool known=false;
        for (size_t i=0;i<ProfileCount;++i) known=known || std::memcmp(p.profileHash,Profiles[i].identifyHash,32)==0;
        if (std::memcmp(p.uuid,uuid,6)!=0 || (enabled && std::memcmp(uuid,selectedUuid,6)!=0)) lastModelLoad=ModelError::Identity;
        else if (std::strcmp(p.surface,surface.c_str())!=0) lastModelLoad=ModelError::Surface;
        else if (!known || (enabled && !connecting && client.GetProfile()!=nullptr && std::memcmp(p.profileHash,client.GetProfile()->identifyHash,32)!=0)) lastModelLoad=ModelError::Profile;
    }
    if (lastModelLoad!=ModelError::None) {
        reply.printf("%s model_error=%s",Text(Message::ModelInvalid,ukrainian),CalibrationModel::ErrorName(lastModelLoad)); return GCodeResult::error;
    }
    if (emergencyPending.load(std::memory_order_acquire) || emergencyGeneration.load(std::memory_order_acquire)!=generation) {
        reply.copy(Text(Message::Session,ukrainian)); return GCodeResult::error;
    }
    model=candidate; std::memcpy(selectedSurface,model.Parameters().surface,sizeof(selectedSurface));
    Status(reply,false); return GCodeResult::ok;
}
bool Pins(const StringRef& reply) noexcept {
    if (!receivePin.AssignPort("d.0",reply,PinUsedBy::sensor,PinAccess::read) ||
        !transmitPin.AssignPort("d.1",reply,PinUsedBy::sensor,PinAccess::read) ||
        receivePin.GetPin()!=PD_0 || transmitPin.GetPin()!=PD_1 || receivePin.GetTotalInvert() || transmitPin.GetTotalInvert()) {
        receivePin.Release(); transmitPin.Release(); return false;
    }
    return true;
}
void DiscoveryStatus(const StringRef& reply) noexcept {
    reply.printf("{\"format\":\"cartographer-v3-discovery-v1\",\"state\":\"%s\",\"nodes\":[",
        discovering?"running":discoveryFault?"fault":discoveryComplete?"complete":"idle");
    for (unsigned i=0;i<discoveryCount;++i) {
        const auto& n=discoveryNodes[i];
        reply.catf("%s{\"uuid\":\"%02x%02x%02x%02x%02x%02x\",\"application\":%u}",i==0?"":",",n.uuid[0],n.uuid[1],n.uuid[2],n.uuid[3],n.uuid[4],n.uuid[5],n.application);
    }
    reply.catf("],\"overflow\":%s}",discoveryOverflow?"true":"false");
}
void Discover(const CanFrame& frame) noexcept {
    if (frame.id!=0x3f1 || (frame.length!=7 && frame.length!=8) || frame.data[0]!=0x20) return;
    const uint8_t application=frame.length==8?frame.data[7]:0;
    for (unsigned i=0;i<discoveryCount;++i) if (std::memcmp(discoveryNodes[i].uuid,frame.data+1,6)==0) {
        if (discoveryNodes[i].application!=application) discoveryFault=true;
        return;
    }
    if (discoveryCount==8) { discoveryOverflow=discoveryFault=true; return; }
    std::memcpy(discoveryNodes[discoveryCount].uuid,frame.data+1,6); discoveryNodes[discoveryCount++].application=application;
}
GCodeResult Settings(GCodeBuffer& gb,const StringRef& reply) THROWS(GCodeException) {
    if (Busy()) { reply.copy(Text(Message::Busy,ukrainian)); return GCodeResult::error; }
    if (!gb.Seen('T')) { reply.copy(Text(Message::Settings,ukrainian)); return GCodeResult::error; }
    const uint32_t touch=gb.GetUIValue();
    if (!gb.Seen('E')) { reply.copy(Text(Message::Settings,ukrainian)); return GCodeResult::error; }
    const float height=gb.GetFValue();
    if (!gb.Seen('V')) { reply.copy(Text(Message::Settings,ukrainian)); return GCodeResult::error; }
    const float speed=gb.GetFValue();
    if (!gb.Seen('D')) { reply.copy(Text(Message::Settings,ukrainian)); return GCodeResult::error; }
    const uint32_t duration=gb.GetUIValue();
    uint32_t threshold=0;
    if (gb.Seen('R')) threshold=gb.GetUIValue();
    const auto& p=model.Parameters();
    if (!model.Loaded() || touch>1 || !std::isfinite(height) || height < -10 || height>100 ||
        (touch==0 && (static_cast<double>(height)<p.heightMin || static_cast<double>(height)>p.heightMax)) || !std::isfinite(speed) || speed<=0 || speed>5 ||
        duration<100 || duration>30000 || (touch!=0 && (threshold==0 || threshold>1000000))) {
        reply.copy(Text(Message::Settings,ukrainian)); return GCodeResult::error;
    }
    settingsTouch=touch!=0; settingsHeight=height; settingsSpeed=speed; settingsDuration=duration; settingsTouchThreshold=threshold; settingsDefined=true;
    Status(reply,false); return GCodeResult::ok;
}
GCodeResult Capture(GCodeBuffer& gb,const StringRef& reply) THROWS(GCodeException) {
    if (!enabled || connecting || client.GetProfile()==nullptr || client.samples==0) { reply.copy(Text(Message::Capture,ukrainian)); return GCodeResult::error; }
    const Sample& s=client.LastSample();
    if (!std::isfinite(s.frequency) || !std::isfinite(s.temperature)) { reply.copy(Text(Message::Capture,ukrainian)); return GCodeResult::error; }
    bool known=gb.Seen('Z'); const float height=known?gb.GetFValue():0;
    if (known && (!std::isfinite(height) || height < -10 || height>100)) { reply.copy(Text(Message::Capture,ukrainian)); return GCodeResult::error; }
    char hash[65];
    static const char hex[]="0123456789abcdef";
    for (unsigned i=0;i<32;++i) { hash[2*i]=hex[client.GetProfile()->identifyHash[i]>>4]; hash[2*i+1]=hex[client.GetProfile()->identifyHash[i]&15]; } hash[64]=0;
    uint64_t frequencyBits,temperatureBits;
    std::memcpy(&frequencyBits,&s.frequency,8); std::memcpy(&temperatureBits,&s.temperature,8);
    // SafeVsnprintf's ARM long accumulator is unsuitable for 17-digit float text.
    // Binary64 bit strings are authoritative; six decimal places are display data.
    reply.printf("{\"format\":\"cartographer-v3-sample-v1\",\"sensor_uuid\":\"%02x%02x%02x%02x%02x%02x\",\"profile_sha256\":\"%s\",\"sample_index\":%" PRIu32 ",\"mcu_clock\":%" PRIu64 ",\"host_ticks\":%" PRIu64 ",\"host_rate\":%" PRIu32 ",\"raw_count\":%" PRIu32 ",\"adc_sum\":%" PRIu32 ",\"frequency_hz\":%.6f,\"temperature_c\":%.6f,\"frequency_bits\":\"%016" PRIx64 "\",\"temperature_bits\":\"%016" PRIx64 "\",\"valid\":%s,\"fresh\":%s",
        selectedUuid[0],selectedUuid[1],selectedUuid[2],selectedUuid[3],selectedUuid[4],selectedUuid[5],hash,client.samples,s.mcuClock,s.hostTime,StepTimer::GetTickRate(),s.count,s.adcSum,s.frequency,s.temperature,frequencyBits,temperatureBits,s.valid?"true":"false",client.HasFreshSample(Now())?"true":"false");
    if (known) reply.catf(",\"height_mm\":%.6f",static_cast<double>(height));
    reply.cat("}"); return GCodeResult::ok;
}
void Supervise(uint64_t now) noexcept {
    const uint32_t flags=localFlags.load(std::memory_order_acquire);
    if ((flags&LocalFailed)!=0) { if ((flags&LocalActive)!=0) client.DisarmTrigger(now); return; }
    if ((flags&LocalActive)==0 || (flags&LocalTriggered)!=0) return;
    // A queued valid hit must never revive an already expired local motion lease.
    if (ProbeFailed()) { client.DisarmTrigger(now); return; }
    if (client.GetTriggerState()==TriggerState::Triggered && client.TriggerReason()==1) { localFlags.fetch_or(LocalTriggered,std::memory_order_release); return; }
    bool healthy=client.GetTriggerState()==TriggerState::Armed && client.HasFreshSample(now) && now<operationDeadline && client.GetProfile()!=nullptr;
    if (healthy) {
        const auto& p=model.Parameters(); const Sample& sample=client.LastSample();
        healthy=model.Loaded() && std::memcmp(p.uuid,selectedUuid,6)==0 && std::memcmp(p.profileHash,client.GetProfile()->identifyHash,32)==0 &&
            std::strcmp(p.surface,selectedSurface)==0 && sample.temperature>=p.temperatureMin && sample.temperature<=p.temperatureMax;
        if (healthy && preparedTouch) healthy=std::fabs(sample.temperature-armedTemperature)<=0.25L;
        else if (healthy) {
            double actual=0;
            healthy=model.Evaluate(armedFrequency,sample.temperature,selectedUuid,client.GetProfile()->identifyHash,selectedSurface,actual)==ModelError::None && std::fabs(actual-static_cast<double>(preparedHeight))<=0.001L;
        }
    }
    const uint64_t report=client.TriggerLastReport();
    if (!healthy || report==0 || now<report || now-report>uint64_t(StepTimer::GetTickRate())*60/1000) { FailOperation(); client.DisarmTrigger(now); return; }
    uint64_t deadline=now+uint64_t(20000-client.SampleAgeUs(now))*StepTimer::GetTickRate()/1000000;
    const uint64_t reportDeadline=report+uint64_t(StepTimer::GetTickRate())*60/1000;
    if (reportDeadline<deadline) deadline=reportDeadline;
    if (operationDeadline<deadline) deadline=operationDeadline;
    localDeadline.store(static_cast<uint32_t>(deadline),std::memory_order_release);
}
}
void Init() noexcept {
    if (!mutexCreated) { runtimeMutex.Create("Cartographer"); mutexCreated=true; }
    MutexLocker lock(runtimeMutex);
    client.Stop(); Release(); model.Clear(); selectedSurface[0]=0; lastModelLoad=ModelError::None;
    scanActive=preparing=settingsDefined=settingsTouch=discovering=discoveryComplete=discoveryFault=discoveryOverflow=false;
    discoveryCount=0; localFlags.store(0); localDeadline.store(0); emergencyPending.store(false,std::memory_order_release); emergencyLatched.store(false,std::memory_order_release);
    previousClock=StepTimer::GetTimerTicks(); extendedClock=previousClock;
}
void Spin() noexcept {
    MutexLocker lock(runtimeMutex,0);
    if (!lock.IsAcquired()) return;
    if (emergencyPending.load(std::memory_order_acquire)) { CompleteEmergency(); return; }
    const uint64_t now=Now();
    if (!enabled && !discovering) return;
    can.Poll();
    if (can.Failed() || can.Overflow()!=0) {
        if (discovering) { discoveryFault=true; discovering=false; }
        if (Busy()) FailOperation();
        client.Fail(Fault::Transport); Release(); return;
    }
    if (discovering) {
        if (can.Ready() && !discoverySent) { const uint8_t query=0; discoverySent=can.Send(0x3f0,&query,1); }
        CanFrame frame;
        for (unsigned budget=0;budget<32 && can.Read(frame);++budget) Discover(frame);
        if (millis()-discoveryStarted>=2000) { discoveryComplete=true; discovering=false; Release(); }
        return;
    }
    if (connecting) {
        if (!can.Ready()) return;
        connecting=false; client.Start(selectedUuid,StepTimer::GetTickRate(),now,triggersWanted);
    }
    CanFrame frame;
    for (unsigned budget=0;budget<32 && can.Read(frame);++budget) if (frame.id==ResponseCanId) {
        const uint64_t drainedAt=Now();
        const uint64_t received=drainedAt-static_cast<uint32_t>(static_cast<uint32_t>(drainedAt)-frame.received);
        client.Receive(frame.data,frame.length,received,drainedAt);
    }
    client.Tick(Now()); Supervise(Now());
    if (client.GetState()==State::Fault || client.GetState()==State::Disabled) {
        if (Busy() && (localFlags.load()&LocalTriggered)==0) FailOperation();
        Release();
    }
}
void Exit() noexcept {
    MutexLocker lock(runtimeMutex);
    if (Busy()) FailOperation();
    client.Stop(); discovering=false; Release();
}
void EmergencyStop() noexcept {
    // Publish failure and stop the local peripheral without waiting for SD/model I/O.
    const auto flags=IrqSave();
    emergencyGeneration.fetch_add(1,std::memory_order_acq_rel);
    emergencyPending.store(true,std::memory_order_release);
    emergencyLatched.store(true,std::memory_order_release);
    localFlags.fetch_or(LocalFailed,std::memory_order_release);
    can.EmergencyAbort();
    IrqRestore(flags);
    if (!mutexCreated.load(std::memory_order_acquire)) return;
    MutexLocker lock(runtimeMutex,0);
    if (lock.IsAcquired()) CompleteEmergency();
}
void Diagnostics(MessageType destination) noexcept {
    MutexLocker lock(runtimeMutex);
    String<1536> reply; Status(reply.GetRef());
    reprap.GetPlatform().MessageF(destination,"%s\n",reply.c_str());
}
bool ReadHeight(float& height) noexcept {
    MutexLocker lock(runtimeMutex); double value=0;
    const bool valid=CurrentHeight(Now(),value)==ModelError::None;
    height=valid?static_cast<float>(value):0; return valid;
}
bool ModelReady() noexcept { float ignored; return ReadHeight(ignored); }
GCodeResult ReadHeightAtState(uint32_t rawTime,float& height) noexcept {
    CalibrationModel snapshot;
    Sample before,after;
    uint8_t uuid[6],hash[32]; char surface[32]; uint64_t target=0;
    height=0;
    {
        MutexLocker lock(runtimeMutex);
        const uint64_t now=Now();
        if (emergencyLatched.load(std::memory_order_acquire) || !enabled || connecting || !model.Loaded() || client.GetProfile()==nullptr || !client.HasFreshSample(now)) return GCodeResult::error;
        const int32_t difference=static_cast<int32_t>(rawTime-static_cast<uint32_t>(now));
        if (difference>0) return difference<=static_cast<int32_t>(StepTimer::GetTickRate()/50)?GCodeResult::notFinished:GCodeResult::error;
        const uint64_t age=static_cast<uint32_t>(-static_cast<int64_t>(difference));
        if (age>now || age>StepTimer::GetTickRate()/50) return GCodeResult::error;
        target=now-age;
        bool found=false; Sample newer;
        if (!client.RecentSample(0,newer)) return GCodeResult::error;
        if (newer.hostTime<target) return GCodeResult::notFinished;
        for (size_t i=0;i<64;++i) {
            Sample current;
            if (!client.RecentSample(i,current)) break;
            if (current.hostTime<=target) { before=current; after=i==0?current:newer; found=true; break; }
            newer=current;
        }
        const uint64_t maxGap=uint64_t(client.GetProfile()->periodUs)*3*StepTimer::GetTickRate()/1000000;
        if (!found || !before.valid || !after.valid || after.hostTime<before.hostTime || after.hostTime-before.hostTime>maxGap) return GCodeResult::error;
        snapshot=model; std::memcpy(uuid,selectedUuid,6); std::memcpy(hash,client.GetProfile()->identifyHash,32); std::memcpy(surface,selectedSurface,32);
    }
    double low=0,high=0;
    if (snapshot.Evaluate(before.frequency,before.temperature,uuid,hash,surface,low)!=ModelError::None ||
        snapshot.Evaluate(after.frequency,after.temperature,uuid,hash,surface,high)!=ModelError::None) return GCodeResult::error;
    const double fraction=after.hostTime==before.hostTime?0:static_cast<double>(target-before.hostTime)/(after.hostTime-before.hostTime);
    height=static_cast<float>(low+(high-low)*fraction);
    return !emergencyLatched.load(std::memory_order_acquire) && std::isfinite(height)?GCodeResult::ok:GCodeResult::error;
}
bool ReadHeightAt(uint32_t rawTime,float& height) noexcept { return ReadHeightAtState(rawTime,height)==GCodeResult::ok; }
bool BeginScan() noexcept {
    MutexLocker lock(runtimeMutex); double ignored;
    if (preparing || (localFlags.load()&LocalActive)!=0 || settingsTouch || CurrentHeight(Now(),ignored)!=ModelError::None) return false;
    scanActive=true; return true;
}
void EndScan() noexcept {
    MutexLocker lock(runtimeMutex,emergencyLatched.load(std::memory_order_acquire)?0:Mutex::TimeoutUnlimited);
    if (!lock.IsAcquired()) return;
    if (emergencyPending.load(std::memory_order_acquire)) CompleteEmergency();
    scanActive=false;
}
GCodeResult PrepareProbe(bool touch,float height,float speed,const StringRef& reply) THROWS(GCodeException) {
    MutexLocker lock(runtimeMutex); const uint64_t now=Now();
    if (!settingsDefined || touch!=settingsTouch || !std::isfinite(height) || std::fabs(height-settingsHeight)>0.00001L || !std::isfinite(speed) || speed<=0 || speed>settingsSpeed*1.001L) {
        reply.copy(Text(Message::Settings,ukrainian)); return GCodeResult::error;
    }
    if (scanActive || (localFlags.load()&LocalActive)!=0) { reply.copy(Text(Message::Busy,ukrainian)); return GCodeResult::error; }
    if (!enabled || connecting || !client.TriggerConfigured()) { reply.copy(Text(Message::Trigger,ukrainian)); return GCodeResult::error; }
    double currentHeight=0;
    if (CurrentHeight(now,currentHeight)!=ModelError::None) { reply.copy(Text(Message::NotReady,ukrainian)); return GCodeResult::error; }
    if (preparing) {
        if (preparedTouch!=touch || preparedHeight!=height || preparedSpeed!=speed || now>=operationDeadline || client.GetTriggerState()==TriggerState::Fault || client.GetTriggerState()==TriggerState::Triggered) {
            FailOperation(); preparing=false; client.DisarmTrigger(now); reply.copy(Text(Message::Prepare,ukrainian)); return GCodeResult::error;
        }
        return client.GetTriggerState()==TriggerState::Armed?GCodeResult::ok:GCodeResult::notFinished;
    }
    if (client.GetTriggerState()==TriggerState::Disarming) return GCodeResult::notFinished;
    uint32_t trigger=0,untrigger=0;
    armedTemperature=client.LastSample().temperature;
    if (!touch) {
        double frequency=0;
        if (model.FrequencyForHeight(height,armedTemperature,selectedUuid,client.GetProfile()->identifyHash,selectedSurface,frequency)!=ModelError::None) {
            reply.copy(Text(Message::Prepare,ukrainian)); return GCodeResult::error;
        }
        const double count=frequency*536870912/client.GetProfile()->clockHz;
        if (!std::isfinite(count) || count<=1 || count>=268435455) { reply.copy(Text(Message::Prepare,ukrainian)); return GCodeResult::error; }
        trigger=static_cast<uint32_t>(std::ceil(count)); untrigger=static_cast<uint32_t>(count*0.994L);
        armedFrequency=static_cast<double>(trigger)*client.GetProfile()->clockHz/536870912;
        double quantizedHeight=0;
        if (model.Evaluate(armedFrequency,armedTemperature,selectedUuid,client.GetProfile()->identifyHash,selectedSurface,quantizedHeight)!=ModelError::None || std::fabs(quantizedHeight-static_cast<double>(height))>0.001L) {
            reply.copy(Text(Message::Prepare,ukrainian)); return GCodeResult::error;
        }
    }
    operationDeadline=now+uint64_t(settingsDuration)*StepTimer::GetTickRate()/1000;
    if (!client.BeginTrigger(touch?TriggerMethod::Touch:TriggerMethod::Scan,trigger,untrigger,settingsTouchThreshold,now,operationDeadline)) {
        reply.copy(Text(Message::Prepare,ukrainian)); return GCodeResult::error;
    }
    preparedTouch=touch; preparedHeight=height; preparedSpeed=speed; preparing=true; localFlags.store(0,std::memory_order_release);
    return GCodeResult::notFinished;
}
bool ArmProbe(bool touch,float height,float speed) noexcept {
    MutexLocker lock(runtimeMutex);
    if (!preparing || preparedTouch!=touch || preparedHeight!=height || preparedSpeed!=speed || client.GetTriggerState()!=TriggerState::Armed) return false;
    const uint64_t now=Now();
    if (!client.HasFreshSample(now) || now>=operationDeadline) return false;
    uint64_t deadline=now+uint64_t(20000-client.SampleAgeUs(now))*StepTimer::GetTickRate()/1000000;
    if (operationDeadline<deadline) deadline=operationDeadline;
    localDeadline.store(static_cast<uint32_t>(deadline),std::memory_order_release);
    preparing=false; localFlags.store(LocalActive,std::memory_order_release); Supervise(now);
    return !ProbeFailed();
}
void DisarmProbe() noexcept {
    MutexLocker lock(runtimeMutex,emergencyLatched.load(std::memory_order_acquire)?0:Mutex::TimeoutUnlimited);
    if (!lock.IsAcquired()) return;
    if (emergencyPending.load(std::memory_order_acquire)) { CompleteEmergency(); return; }
    localFlags.fetch_and(~LocalActive,std::memory_order_release); preparing=false;
    client.DisarmTrigger(Now());
}
bool ProbeFailed() noexcept {
    if (emergencyLatched.load(std::memory_order_acquire)) return true;
    const uint32_t flags=localFlags.load(std::memory_order_acquire);
    if ((flags&LocalFailed)!=0) return true;
    if ((flags&LocalActive)!=0 && (flags&LocalTriggered)==0 && static_cast<int32_t>(StepTimer::GetTimerTicks()-localDeadline.load(std::memory_order_acquire))>=0) {
        localFlags.fetch_or(LocalFailed,std::memory_order_acq_rel); return true;
    }
    return false;
}
bool ProbeTriggered() noexcept { return (localFlags.load(std::memory_order_acquire)&LocalTriggered)!=0 && !ProbeFailed(); }
bool TouchMode() noexcept { MutexLocker lock(runtimeMutex); return settingsTouch; }
float TouchTriggerHeight() noexcept { MutexLocker lock(runtimeMutex); return settingsHeight; }
float TouchProbeSpeed() noexcept { MutexLocker lock(runtimeMutex); return settingsSpeed; }
float TouchThreshold() noexcept { MutexLocker lock(runtimeMutex); return static_cast<float>(settingsTouchThreshold); }
bool IsUkrainian() noexcept { MutexLocker lock(runtimeMutex); return ukrainian; }
GCodeResult Configure(GCodeBuffer& gb,const StringRef& reply) THROWS(GCodeException) {
    MutexLocker lock(runtimeMutex,0);
    if (!lock.IsAcquired()) {
        if (emergencyPending.load(std::memory_order_acquire)) { reply.copy(Text(Message::Session,ukrainian)); return GCodeResult::error; }
        return GCodeResult::notFinished;
    }
    if (emergencyPending.load(std::memory_order_acquire)) { CompleteEmergency(); reply.copy(Text(Message::Session,ukrainian)); return GCodeResult::error; }
    if (gb.Seen('L')) {
        String<4> language; gb.GetQuotedString(language.GetRef());
        if (std::strcmp(language.c_str(),"en")==0) ukrainian=false;
        else if (std::strcmp(language.c_str(),"uk")==0) ukrainian=true;
        else { reply.copy(Text(Message::Language,ukrainian)); return GCodeResult::error; }
    }
    if (!gb.Seen('S')) { Status(reply,false); return GCodeResult::ok; }
    const uint32_t action=gb.GetUIValue();
    if (action>8) { reply.copy(Text(Message::Parameters,ukrainian)); return GCodeResult::error; }
    if (action==2) {
        const GCodeResult result=LoadModel(gb,reply);
        if (emergencyPending.load(std::memory_order_acquire)) { CompleteEmergency(); reply.copy(Text(Message::Session,ukrainian)); return GCodeResult::error; }
        return result;
    }
    if (action==3) {
        if (Busy()) { reply.copy(Text(Message::Busy,ukrainian)); return GCodeResult::error; }
        model.Clear(); selectedSurface[0]=0; settingsDefined=false; Status(reply,false); return GCodeResult::ok;
    }
    if (action==4) {
        if (enabled || discovering || Busy()) { reply.copy(Text(Message::Discovery,ukrainian)); return GCodeResult::error; }
        if (!Pins(reply)) { reply.copy(Text(Message::Pins,ukrainian)); return GCodeResult::error; }
        client.Stop(); discoveryCount=0; discoverySent=discoveryComplete=discoveryFault=discoveryOverflow=false;
        discovering=true; discoveryStarted=millis();
        if (!can.Start(&emergencyPending)) { discovering=false; Release(); reply.copy(Text(Message::Session,ukrainian)); return GCodeResult::error; }
        DiscoveryStatus(reply); return GCodeResult::ok;
    }
    if (action==5) { DiscoveryStatus(reply); return GCodeResult::ok; }
    if (action==6) return Settings(gb,reply);
    if (action==7) return Capture(gb,reply);
    if (action==8) {
        if (emergencyLatched.load(std::memory_order_acquire) || !enabled || client.GetState()==State::Fault) { reply.copy(Text(Message::Session,ukrainian)); return GCodeResult::error; }
        const uint64_t now=Now(); double ignored;
        if (!connecting && client.HasFreshSample(now) && (!model.Loaded() || CurrentHeight(now,ignored)==ModelError::None)) { Status(reply,false); return GCodeResult::ok; }
        if (now-connectedAt>uint64_t(StepTimer::GetTickRate())*10) { reply.copy(Text(Message::Wait,ukrainian)); return GCodeResult::error; }
        return GCodeResult::notFinished;
    }
    if (action==0) {
        if (Busy()) FailOperation();
        if (discovering) { discovering=false; Release(); }
        if (connecting || !enabled) Exit(); else client.RequestStop(Now());
        Status(reply,false); return GCodeResult::ok;
    }
    if (enabled || discovering || Busy()) { reply.copy(Text(Message::Active,ukrainian)); return GCodeResult::error; }
    uint8_t uuid[6]={};
    if (!Uuid(gb,uuid)) { reply.copy(Text(Message::Uuid,ukrainian)); return GCodeResult::error; }
    const uint32_t triggers=gb.Seen('H')?gb.GetUIValue():0;
    if (triggers>1) { reply.copy(Text(Message::Parameters,ukrainian)); return GCodeResult::error; }
    if (!Pins(reply)) { reply.copy(Text(Message::Pins,ukrainian)); return GCodeResult::error; }
    std::memcpy(selectedUuid,uuid,6); client.Stop(); enabled=connecting=true; triggersWanted=triggers!=0;
    connectedAt=Now();
    if (!can.Start(&emergencyPending)) { Release(); reply.copy(Text(Message::Session,ukrainian)); return GCodeResult::error; }
    const auto irqFlags=IrqSave();
    const bool interrupted=emergencyPending.load(std::memory_order_acquire);
    if (!interrupted) { localFlags.store(0); emergencyLatched.store(false,std::memory_order_release); }
    IrqRestore(irqFlags);
    if (interrupted) { CompleteEmergency(); reply.copy(Text(Message::Session,ukrainian)); return GCodeResult::error; }
    Status(reply,false); return GCodeResult::ok;
}
}
