// Host hardware/scheduler adapter for unchanged pinned Cartographer MCU protocol code.
// The sensor readout and base/config storage are modelled; command framing, CAN
// assignment, ACK/NAK and trsync state/timers are upstream C, not a second codec.
#include "compiler.h"
#include "command.h"
#include "sched.h"
#include "basecmd.h"
#include "generic/canbus.h"
#include "generic/canserial.h"
#include "generic/misc.h"
#include "trsync.h"
#include "fasthash.h"
#include "peer.h"
#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
static const uint8_t raw_uuid[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
static uint64_t clock64;
static unsigned configured, config_crc, allocated, home_active, home_method, home_calls, is_shutdown;
static struct timer* timers[8];
static union { max_align_t alignment; uint8_t data[256]; } object;
static void* object_type;
static jmp_buf shutdown_jump;
static unsigned can_feed_jump;
extern void canserial_rx_task(void);
extern void canserial_tx_task(void);
extern void trsync_task(void);
void peer_uuid(uint8_t result[6]) {
    uint64_t hash=fasthash64(raw_uuid,sizeof(raw_uuid),0xA16231A7);
    memcpy(result,&hash,6); canserial_set_uuid((uint8_t*)raw_uuid,sizeof(raw_uuid));
}
uint32_t timer_read_time(void) { return (uint32_t)clock64; }
uint32_t timer_from_us(uint32_t us) { return us*48; }
uint8_t timer_is_before(uint32_t a,uint32_t b) { return (int32_t)(a-b)<0; }
void* console_receive_buffer(void) { return NULL; } // no inbound buffer commands in this fixture
void sched_wake_task(struct task_wake* wake) { wake->wake=1; }
uint8_t sched_check_wake(struct task_wake* wake) { unsigned v=wake->wake; wake->wake=0; return v; }
uint8_t sched_is_shutdown(void) { return is_shutdown; }
void sched_report_shutdown(void) { sendf("is_shutdown static_string_id=%c",1u); }
void sched_shutdown(uint_fast8_t reason) { (void)reason; is_shutdown=1; if (can_feed_jump) longjmp(shutdown_jump,1); abort(); }
void sched_try_shutdown(uint_fast8_t reason) { sched_shutdown(reason); }
void sched_add_timer(struct timer* timer) {
    for (unsigned i=0;i<8;++i) if (timers[i]==timer) return;
    for (unsigned i=0;i<8;++i) if (!timers[i]) { timers[i]=timer; return; }
    abort();
}
void sched_del_timer(struct timer* timer) { for (unsigned i=0;i<8;++i) if (timers[i]==timer) timers[i]=NULL; }
void* oid_alloc(uint8_t oid,void* type,uint16_t size) { assert(allocated && !object_type && oid==0 && size<=sizeof(object)); object_type=type; memset(&object,0,sizeof(object)); return object.data; }
void* oid_lookup(uint8_t oid,void* type) { assert(oid==0 && object_type==type); return object.data; }
void* oid_next(uint8_t* i,void* type) { ++*i; if (*i!=0 || object_type!=type) return NULL; return object.data; }
int canbus_send(struct canbus_msg* msg) { peer_output((uint16_t)msg->id,msg->data,msg->dlc); return 1; }
void canbus_set_filter(uint32_t id) { (void)id; }
void canhw_get_status(struct canbus_status* s) { memset(s,0,sizeof(*s)); }
void bootloader_request(void) { abort(); }
void peer_feed(uint16_t id,const uint8_t* bytes,unsigned length) {
    struct canbus_msg msg={.id=id,.dlc=length}; assert(length<=8); memcpy(msg.data,bytes,length);
    can_feed_jump=1;
    if (!setjmp(shutdown_jump)) {
        canserial_process_data(&msg);
        for (unsigned i=0;i<64;++i) { canserial_rx_task(); trsync_task(); canserial_tx_task(); }
    }
    can_feed_jump=0;
}
void peer_advance(uint64_t clock) {
    clock64=clock;
    for (unsigned budget=0;budget<100;++budget) {
        struct timer* due=NULL;
        for (unsigned i=0;i<8;++i) if (timers[i] && !timer_is_before((uint32_t)clock64,timers[i]->waketime)) { due=timers[i]; break; }
        if (!due) break;
        sched_del_timer(due);
        if (due->func(due)==SF_RESCHEDULE) sched_add_timer(due);
        assert(budget!=99);
    }
    trsync_task(); canserial_tx_task();
}
void peer_tick(uint64_t hostTicks) { peer_advance(hostTicks*48+0xfffffff0ull); }
void peer_sample(uint32_t count,uint32_t adc) {
    sendf("cartographer_data clock=%u data=%u temp=%u",(uint32_t)clock64,count,adc);
    canserial_tx_task();
}
void peer_trigger_reason(unsigned reason) { assert(home_active); trsync_do_trigger(trsync_oid_lookup(0),(uint8_t)reason); trsync_task(); canserial_tx_task(); }
void peer_hit(void) { peer_trigger_reason(1); }
void peer_configure_foreign(void) { configured=1; config_crc=0xdeadbeef; }
unsigned peer_shutdown(void) { return is_shutdown; }
unsigned peer_home_calls(void) { return home_calls; }
unsigned peer_home_active(void) { return home_active; }
void peer_report(unsigned oid,unsigned canTrigger,unsigned reason,uint32_t clock) {
    sendf("trsync_state oid=%c can_trigger=%c trigger_reason=%c clock=%u",oid,canTrigger,reason,clock); canserial_tx_task();
}
unsigned peer_home_method(void) { return home_method; }
void command_identify(uint32_t* args) {
    uint32_t offset=args[0], count=args[1]; assert(offset<=command_identify_size);
    if (count>command_identify_size-offset) count=command_identify_size-offset;
    sendf("identify_response offset=%u data=%.*s",offset,count,(uint8_t*)command_identify_data+offset);
}
void command_get_config(uint32_t* args) { (void)args; sendf("config is_config=%c crc=%u is_shutdown=%c move_count=%hu",configured,config_crc,is_shutdown,0u); }
void command_allocate_oids(uint32_t* args) { assert(!allocated && !configured && args[0]==1); allocated=1; }
void command_finalize_config(uint32_t* args) { assert(allocated && object_type && !configured); configured=1; config_crc=args[0]; }
void command_get_uptime(uint32_t* args) { (void)args; sendf("uptime high=%u clock=%u",(uint32_t)(clock64>>32),(uint32_t)clock64); }
void command_cartographer_base_read(uint32_t* args) {
    assert(args[0]==6 && args[1]==0); uint8_t b[]={0xfc,0x47,0xf6,0x01,0x77,0xda};
    sendf("cartographer_base_data bytes=%*s offset=%hu",6u,b,0u);
}
void command_cartographer_stream(uint32_t* args) { assert(args[0]<=1); }
void command_cartographer_set_threshold(uint32_t* args) { assert(args[0]>args[1] && args[1]>0); }
void command_cartographer_home(uint32_t* args) { assert(args[0]==0 && args[1]==1 && args[2]==0 && args[4]<=1); home_active=1; home_method=args[4]; ++home_calls; }
void command_cartographer_stop_home(uint32_t* args) { (void)args; home_active=0; }
void peer_unexpected(uint32_t* args) { (void)args; abort(); }
