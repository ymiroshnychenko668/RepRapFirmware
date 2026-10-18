#pragma once
#include "CalibrationModel.h"
#include "Protocol.h"
#include <array>
#include <cstring>
#include <cstdint>
namespace CartographerTest {
using namespace RrfModules::CartographerV3;
using Bytes=std::array<uint8_t,CalibrationFileSize>;
inline void UInt(Bytes& b,size_t at,uint32_t v) { for(unsigned i=0;i<4;++i) b[at+i]=static_cast<uint8_t>(v>>(8*i)); }
inline void Number(Bytes& b,size_t at,double v) { uint64_t bits; std::memcpy(&bits,&v,8); for(unsigned i=0;i<8;++i) b[at+i]=static_cast<uint8_t>(bits>>(8*i)); }
inline void Seal(Bytes& b) { UInt(b,316,CalibrationModel::Crc32(b.data(),316)); }
inline Bytes Model(bool temperature=false) {
    Bytes b={}; std::memcpy(b.data(),"CARTOV3\0",8); UInt(b,8,1); UInt(b,12,320);
    const uint8_t uuid[]={0x12,0x34,0x56,0x78,0x9a,0xbc}; std::memcpy(b.data()+16,uuid,6);
    std::memcpy(b.data()+24,Profiles[0].identifyHash,32); std::memcpy(b.data()+56,"pei",4);
    UInt(b,88,temperature?1:0); UInt(b,92,100); UInt(b,96,100);
    Number(b,104,2.5e-7); Number(b,112,1.0/3000000);
    const double c[]={2.75,2.5,.06,.02,-.008,.005,.003,-.002,.001,.0005};
    for(unsigned i=0;i<10;++i) Number(b,120+8*i,c[i]);
    Number(b,200,25); Number(b,208,temperature?20:24.5); Number(b,216,temperature?60:25.5); Number(b,224,-.12);
    Number(b,232,.1625000000000002); Number(b,240,5.209499999999999); Number(b,248,.0001); Number(b,256,.00005);
    if(temperature) {
        const double t[]={5.367848131964879e-06,1.7559290784042338,.0007546669217813525,-343.290029518937,2943053.8415908813};
        for(unsigned i=0;i<5;++i) Number(b,264+8*i,t[i]);
    }
    Seal(b); return b;
}
}
