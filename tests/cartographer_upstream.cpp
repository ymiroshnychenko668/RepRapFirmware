// Production RRF client talking to unchanged pinned MCU command.c/canserial.c/trsync.c.
#include "Client.h"
#include "cartographer_upstream/peer.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>
using namespace RrfModules::CartographerV3;
struct Frame { uint16_t id; std::vector<uint8_t> bytes; };
static std::deque<Frame> incoming;
static uint64_t now=1000000;
static bool dropOutput=false;
extern "C" void peer_output(uint16_t id,const uint8_t* bytes,unsigned length) { if (!dropOutput) incoming.push_back({id,{bytes,bytes+length}}); }
struct Bus final:Transport {
    std::deque<Frame> outgoing;
    unsigned capacity=1000, maximum=1000;
    bool corrupt=false, loseReply=false;
    bool Send(uint16_t id,const uint8_t* p,size_t n) noexcept override {
        if (!capacity) return false;
        --capacity; outgoing.push_back({id,{p,p+n}}); return true;
    }
} bus;
static Client client(bus);
static void Pump() {
    for (unsigned budget=0;!bus.outgoing.empty() || !incoming.empty();++budget) {
        assert(budget<10000);
        while (!bus.outgoing.empty()) { auto f=bus.outgoing.front(); bus.outgoing.pop_front();
            if (f.id==CommandCanId && bus.corrupt) { f.bytes[3]^=1; bus.corrupt=false; }
            dropOutput=f.id==CommandCanId && bus.loseReply;
            peer_feed(f.id,f.bytes.data(),f.bytes.size());
            if (dropOutput) bus.loseReply=false;
            dropOutput=false; }
        while (!incoming.empty()) { const auto f=incoming.front(); incoming.pop_front(); if (f.id==ResponseCanId) client.Receive(f.bytes.data(),f.bytes.size(),now,now); }
    }
}
static void Step(unsigned milliseconds=1,bool samples=true,bool peerTimers=true) {
    for (unsigned i=0;i<milliseconds;++i) {
        now+=1000; bus.capacity=bus.maximum;
        if (peerTimers) peer_advance(now*48+0xfffffff0ull);
        client.Tick(now); Pump();
        if (samples) { peer_sample(32917500,55927); Pump(); }
    }
}
int main(int argc,char** argv) {
    const std::string scenario=argc>1?argv[1]:"scan";
    uint8_t uuid[6]; peer_uuid(uuid);
    bus.corrupt=scenario=="crc-recovery"; bus.loseReply=scenario=="ack-response-loss";
    if (scenario=="backpressure") bus.capacity=bus.maximum=1;
    if (scenario=="foreign") peer_configure_foreign();
    client.Start(uuid,1000000,now,scenario!="diagnostic"); Pump(); Step(2000);
    if (scenario=="foreign") { assert(client.GetFault()==Fault::Identity && peer_home_calls()==0); std::puts("upstream peer: foreign configuration rejected"); return 0; }
    assert(!peer_shutdown());
    assert(client.HasFreshSample(now));
    if (scenario=="crc-recovery" || scenario=="ack-response-loss") assert(client.retries>0);
    if (scenario=="diagnostic") { assert(!client.TriggerConfigured() && peer_home_calls()==0); std::puts("upstream peer: diagnostic session does not configure or arm"); return 0; }
    assert(client.TriggerConfigured());
    if (scenario=="owned-reconnect") {
        client.Stop(); client.Start(uuid,1000000,now,true); Pump(); Step(2200);
        assert(client.HasFreshSample(now) && client.TriggerConfigured() && !peer_shutdown());
    }
    assert(client.BeginTrigger(scenario=="touch"?TriggerMethod::Touch:TriggerMethod::Scan,40000000,39000000,2500,now,now+1000000));
    Step(30); assert(client.GetTriggerState()==TriggerState::Armed);
    assert(peer_home_calls()==1 && peer_home_method()==(scenario=="touch"?1u:0u) && peer_home_threshold()==2500);
    if (scenario=="local-timeout") { Step(1100); assert(client.GetFault()==Fault::Timeout && client.GetTriggerState()==TriggerState::Fault); }
    else if (scenario=="lost-reports") {
        // Deliver no remote messages while host clock advances. Local supervision
        // must reject the session even though raw data/transport previously worked.
        for (unsigned i=0;i<70;++i) {
            now+=1000; bus.capacity=bus.maximum;
            dropOutput=true; peer_tick(now); dropOutput=false;
            peer_sample(32917500,55927); Pump(); client.Tick(now); Pump();
        }
        assert(client.GetTriggerState()==TriggerState::Fault);
    } else if (scenario=="normal-stop") {
        client.RequestStop(now); Step(20);
        assert(client.GetState()==State::Disabled && !peer_home_active() && client.TriggerReason()!=1);
    } else if (scenario=="bad-oid") {
        peer_report(1,0,1,static_cast<uint32_t>(now*48+0xfffffff0ull)); Pump();
        assert(client.GetTriggerState()==TriggerState::Fault && client.GetFault()==Fault::Protocol && client.TriggerReason()!=1);
    } else if (scenario=="stale-report") {
        peer_report(0,0,1,static_cast<uint32_t>((now-100000)*48+0xfffffff0ull)); Pump();
        assert(client.GetTriggerState()==TriggerState::Fault && client.GetFault()==Fault::Stale && client.TriggerReason()!=1);
    } else if (scenario=="invalid-sample") {
        peer_sample(0x0fffffff,55927); Pump(); client.Tick(++now);
        assert(client.GetTriggerState()==TriggerState::Fault && client.TriggerReason()!=1);
    } else if (scenario=="unexpected-reason") {
        peer_trigger_reason(2); Pump();
        assert(client.GetTriggerState()==TriggerState::Fault && client.TriggerReason()==2 && client.GetFault()==Fault::Protocol);
    } else if (scenario=="remote-timeout") {
        // The real MCU expiry timer must report CommsTimeout, never EndstopHit.
        now+=110000; peer_advance(now*48+0xfffffff0ull); Pump();
        assert(client.GetTriggerState()==TriggerState::Fault && client.TriggerReason()==4 && client.GetFault()==Fault::Timeout);
    } else if (scenario=="node-conflict") {
        uint8_t assignment[8]={1}; std::memcpy(assignment+1,uuid,6); assignment[1]^=1; assignment[7]=NodeId;
        peer_feed(0x3f0,assignment,8); assert(peer_shutdown()); Step(120);
        assert(client.GetTriggerState()==TriggerState::Fault && client.TriggerReason()!=1);
    } else {
        Step(150); assert(client.GetTriggerState()==TriggerState::Armed); // includes remote timeout extension
        peer_hit(); Pump();
        assert(client.GetTriggerState()==TriggerState::Triggered && client.TriggerReason()==1 && client.TriggerTime()!=0);
        const auto hit=client.TriggerTime(); client.DisarmTrigger(now); Step(10);
        assert(client.GetTriggerState()==TriggerState::Disabled && client.TriggerReason()==1 && client.TriggerTime()==hit);
    }
    std::printf("upstream peer: %s passed (actual MCU CAN, ACK/NAK, config and trsync timers)\n",scenario.c_str());
}
