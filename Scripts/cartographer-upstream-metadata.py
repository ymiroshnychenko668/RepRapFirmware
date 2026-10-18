#!/usr/bin/env python3
"""Generate test-only Klipper command tables from a preserved official binary."""
import importlib.util
import json
from pathlib import Path
import sys
sys.dont_write_bytecode = True  # Verification must leave the source checkout clean.
spec=importlib.util.spec_from_file_location('profiles',Path(__file__).with_name('cartographer-profiles.py'))
m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
compressed,d=m.extract(Path(sys.argv[1]).read_bytes())
handlers={name: 'command_'+name for name in ('identify','get_config','allocate_oids','finalize_config','get_uptime','get_canbus_id','cartographer_base_read','cartographer_stream','cartographer_set_threshold','cartographer_home','cartographer_stop_home','config_trsync','trsync_start','trsync_set_timeout','trsync_trigger')}
lengths={'%u':5,'%i':5,'%hu':3,'%hi':3,'%c':2,'%s':64,'%*s':64,'%.*s':64}
types={'%u':'PT_uint32','%i':'PT_int32','%hu':'PT_uint16','%hi':'PT_int16','%c':'PT_byte','%s':'PT_string','%*s':'PT_buffer','%.*s':'PT_progmem_buffer'}
print('#include "compiler.h"\n#include "command.h"\n#include <string.h>\n#include <stdlib.h>')
for handler in list(handlers.values())+['peer_unexpected']: print(f'void {handler}(uint32_t* args);')
for kind in ('commands','responses'):
    for fmt,i in d[kind].items():
        params=[types[p.split('=')[1]] for p in fmt.split()[1:]]
        print(f'static const uint8_t {kind}_{i}[]={{'+','.join(params or ['0'])+'};')
print('const struct command_parser command_index[]={')
for fmt,i in sorted(d['commands'].items(),key=lambda x:x[1]):
    name=fmt.split()[0]; n=len(fmt.split())-1
    handler=handlers.get(name,'peer_unexpected'); flags=1 if name in ('identify','get_config','get_uptime','get_canbus_id') else 0
    print(f'[{i}]={{.encoded_msgid={i},.num_args={n},.flags={flags},.num_params={n},.param_types=commands_{i},.func={handler}}},')
print('};\nconst uint16_t command_index_size=sizeof(command_index)/sizeof(command_index[0]);')
for fmt,i in d['responses'].items():
    n=len(fmt.split())-1
    max_size=min(64,6+sum(lengths[p.split('=')[1]] for p in fmt.split()[1:]))
    print(f'static const struct command_encoder encoder_{i}={{.encoded_msgid={i},.max_size={max_size},.num_params={n},.param_types=responses_{i}}};')
print('const struct command_encoder* ctr_lookup_encoder(const char* fmt) {')
for fmt,i in d['responses'].items(): print(f'if (!strcmp(fmt,{json.dumps(fmt)})) return &encoder_{i};')
print('abort(); }\nuint8_t ctr_lookup_static_string(const char* s) { (void)s; return 1; }')
print('const uint8_t command_identify_data[]={'+','.join(str(v) for v in compressed)+'};\nconst uint32_t command_identify_size=sizeof(command_identify_data);')
