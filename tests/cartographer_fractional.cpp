#include <GCodes/FractionalCommand.h>
#include <cassert>
#include <cstdio>
int main() {
    for(unsigned code=0;code<1000;++code) {
        bool expected=code==36||code==201||code==260||code==261||code==505||code==569||code==586||code==587;
        if(code==558)expected=SUPPORT_SCANNING_PROBES||RRF_MODULE_CARTOGRAPHER_V3;
        if(code==970)expected=SUPPORT_PHASE_STEPPING;
        assert(SupportsFractionalMCode(code)==expected);
    }
    std::puts("RRF fractional M-code gate passed");
}
