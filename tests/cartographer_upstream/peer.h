#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void peer_uuid(uint8_t result[6]);
void peer_feed(uint16_t id, const uint8_t* bytes, unsigned length);
// One peer coldboot per test process; upstream static protocol/OID state is retained.
// clock is the MCU 48 MHz timer, including its explicit high uptime word.
void peer_advance(uint64_t clock);
void peer_tick(uint64_t hostTicks);
void peer_sample(uint32_t count, uint32_t adc);
void peer_hit(void);
void peer_trigger_reason(unsigned reason);
void peer_configure_foreign(void);
unsigned peer_home_calls(void);
unsigned peer_home_active(void);
void peer_report(unsigned oid, unsigned canTrigger, unsigned reason, uint32_t clock);
unsigned peer_shutdown(void);
unsigned peer_home_method(void);
void peer_output(uint16_t id,const uint8_t* bytes,unsigned length);
#ifdef __cplusplus
}
#endif
