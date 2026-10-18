#include "CalibrationModel.h"
#include "cartographer_model_data.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
using namespace RrfModules::CartographerV3;
using namespace CartographerTest;
static ModelError Evaluate(const CalibrationModel& m,double f,double t,double& h,double* corrected=nullptr) {
    const auto& p=m.Parameters(); return m.Evaluate(f,t,p.uuid,p.profileHash,p.surface,h,corrected);
}
static void InverseRoundTrip(const CalibrationModel& m,double temperature) {
    const auto& p=m.Parameters();
    for (unsigned i=0;i<=1000;++i) {
        const double expected=p.heightMin+(p.heightMax-p.heightMin)*i/1000;
        double frequency=0,actual=0;
        assert(m.FrequencyForHeight(expected,temperature,p.uuid,p.profileHash,p.surface,frequency)==ModelError::None);
        // Endpoint reciprocal rounding is tested separately; interior values must
        // retain far more precision than the one-micron trigger requirement.
        if (i!=0 && i!=1000) {
            assert(Evaluate(m,frequency,temperature,actual)==ModelError::None);
            assert(std::fabs(actual-expected)<1e-8);
        }
        assert(std::isfinite(frequency) && frequency>0);
    }
    double frequency=42;
    assert(m.FrequencyForHeight(p.heightMin-1e-6,temperature,p.uuid,p.profileHash,p.surface,frequency)==ModelError::Range && frequency==0);
    assert(m.FrequencyForHeight(p.heightMax+1e-6,temperature,p.uuid,p.profileHash,p.surface,frequency)==ModelError::Range);
    assert(m.FrequencyForHeight(p.heightMin,p.temperatureMax+1,p.uuid,p.profileHash,p.surface,frequency)==ModelError::Temperature);
    assert(m.FrequencyForHeight(p.heightMin,temperature,nullptr,p.profileHash,p.surface,frequency)==ModelError::Identity);
    assert(m.FrequencyForHeight(p.heightMin,temperature,p.uuid,nullptr,p.surface,frequency)==ModelError::Profile);
    assert(m.FrequencyForHeight(p.heightMin,temperature,p.uuid,p.profileHash,"glass",frequency)==ModelError::Surface);
}
static void Reject(CalibrationModel& m,Bytes b,ModelError expected) {
    Seal(b); assert(m.Load(b.data(),b.size())==expected); assert(m.Loaded());
    double h; assert(Evaluate(m,3500000,25,h)==ModelError::None && std::fabs(h-2.2740197222642298)<1e-10);
}
int main(int argc,char** argv) {
    if(argc==3 && std::strcmp(argv[1],"--batch")==0) {
        FILE* file=std::fopen(argv[2],"rb"); assert(file); Bytes b; assert(std::fread(b.data(),1,b.size(),file)==b.size()); assert(std::fgetc(file)==EOF); std::fclose(file);
        CalibrationModel model; const ModelError loaded=model.Load(b.data(),b.size());
        if(loaded!=ModelError::None) { std::fprintf(stderr,"%s\n",CalibrationModel::ErrorName(loaded)); return 1; }
        double frequency,temperature;
        while(std::scanf("%lf,%lf",&frequency,&temperature)==2) {
            double h=0,corrected=0; const ModelError e=Evaluate(model,frequency,temperature,h,&corrected);
            std::printf("%s,%.17g,%.17g\n",CalibrationModel::ErrorName(e),h,corrected);
        }
        return 0;
    }
    assert(CalibrationModel::Crc32(reinterpret_cast<const uint8_t*>("123456789"),9)==0xcbf43926u);
    CalibrationModel m; double height=42;
    assert(Evaluate(m,3500000,25,height)==ModelError::Missing && height==0);
    assert(m.Load(nullptr,320)==ModelError::Format);
    Bytes b=Model(); assert(m.Load(b.data(),319)==ModelError::Format);
    assert(m.Load(b.data(),320)==ModelError::None);
    InverseRoundTrip(m,25);
    const double frequencies[]={4000000,3800000,3500000,3200000,3000000};
    const double expected[]={.1625000000000002,.9391329104360225,2.2740197222642298,3.8971923828124995,5.209499999999999};
    for(unsigned i=0;i<5;++i) assert(Evaluate(m,frequencies[i],25,height)==ModelError::None && std::fabs(height-expected[i])<1e-10);
    assert(Evaluate(m,4000001,25,height)==ModelError::Domain && height==0);
    assert(Evaluate(m,2999999,25,height)==ModelError::Domain);
    assert(Evaluate(m,3500000,25.6,height)==ModelError::Temperature);
    assert(Evaluate(m,std::numeric_limits<double>::infinity(),25,height)!=ModelError::None);
    assert(Evaluate(m,3500000,std::numeric_limits<double>::quiet_NaN(),height)!=ModelError::None);
    auto p=m.Parameters(); p.uuid[0]^=1;
    assert(m.Evaluate(3500000,25,p.uuid,p.profileHash,p.surface,height)==ModelError::Identity);
    assert(m.Evaluate(3500000,25,nullptr,p.profileHash,p.surface,height)==ModelError::Identity);
    p=m.Parameters(); p.profileHash[0]^=1;
    assert(m.Evaluate(3500000,25,p.uuid,p.profileHash,p.surface,height)==ModelError::Profile);
    assert(m.Evaluate(3500000,25,p.uuid,nullptr,p.surface,height)==ModelError::Profile);
    p=m.Parameters(); assert(m.Evaluate(3500000,25,p.uuid,p.profileHash,"glass",height)==ModelError::Surface);
    b=Model(); b[120]^=1; assert(m.Load(b.data(),320)==ModelError::Integrity && m.Loaded());
    b=Model(); b[22]=1; Reject(m,b,ModelError::Format);
    b=Model(); UInt(b,88,2); Reject(m,b,ModelError::Format);
    b=Model(); Number(b,120,std::numeric_limits<double>::quiet_NaN()); Reject(m,b,ModelError::Coefficients);
    b=Model(); Number(b,112,2.5e-7); Reject(m,b,ModelError::Domain);
    b=Model(); Number(b,248,.00101); Reject(m,b,ModelError::Quality);
    b=Model(); UInt(b,96,9); Reject(m,b,ModelError::Quality);
    b=Model(); Number(b,216,27); Reject(m,b,ModelError::Temperature);
    b=Model(); Number(b,264,-0.0); Reject(m,b,ModelError::Temperature);
    b=Model(); Number(b,240,.1625000000000002); Reject(m,b,ModelError::Range);
    b=Model(); b[60]=1; Reject(m,b,ModelError::Surface); // hidden nonzero padding
    b=Model(); std::memset(b.data()+16,255,6); Reject(m,b,ModelError::Identity);
    // Positive derivative requiring subdivision; then a narrow negative interval between grid points.
    b=Model();
    for(unsigned i=1;i<10;++i) Number(b,120+8*i,0);
    Number(b,128,.01+.123*.123); Number(b,136,-.123); Number(b,144,1.0/3);
    Number(b,232,2.75-(.01+.123*.123)-.123-1.0/3-.12);
    Number(b,240,2.75+(.01+.123*.123)-.123+1.0/3-.12); Seal(b);
    CalibrationModel subdivision; assert(subdivision.Load(b.data(),320)==ModelError::None);
    Number(b,128,.123*.123-1e-7);
    Number(b,232,2.75-(.123*.123-1e-7)-.123-1.0/3-.12);
    Number(b,240,2.75+(.123*.123-1e-7)-.123+1.0/3-.12);
    Reject(m,b,ModelError::Monotonic);
    b=Model(true); assert(m.Load(b.data(),320)==ModelError::None);
    InverseRoundTrip(m,20); InverseRoundTrip(m,25); InverseRoundTrip(m,40);
    double corrected;
    assert(Evaluate(m,3500000,40,height,&corrected)==ModelError::None && std::fabs(height-2.301363780648306)<1e-10 && std::fabs(corrected-3494391.9708091198)<1e-6);
    assert(Evaluate(m,3800000,20,height)==ModelError::None && std::fabs(height-.9273465665377051)<1e-10);
    b=Model(true); for(unsigned i=0;i<4;++i) Number(b,264+8*i,0); Seal(b); assert(m.Load(b.data(),320)==ModelError::Temperature);
    assert(Evaluate(m,3500000,40,height)==ModelError::None); // failed replacement retained the old thermal model
    m.Clear(); assert(!m.Loaded() && Evaluate(m,3500000,40,height)==ModelError::Missing);
    std::puts("Cartographer height: upstream goldens, CRC/format/identity/domain/temperature, monotonic proof and transactional replacement passed");
}
