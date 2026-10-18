#include "CalibrationModel.h"
#include <cmath>
#include <cstring>
#include <limits>

namespace RrfModules::CartographerV3 {
namespace {
uint32_t UInt(const uint8_t* p) noexcept { return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
double Number(const uint8_t* p) noexcept {
    static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559, "Calibration requires IEEE binary64");
    const uint64_t bits = UInt(p) | (uint64_t(UInt(p + 4)) << 32);
    double value; std::memcpy(&value, &bits, sizeof(value)); return value;
}
bool In(double value, double low, double high) noexcept { return std::isfinite(value) && value >= low && value <= high; }
bool Zero(const uint8_t* p, size_t n) noexcept { for (size_t i = 0; i < n; ++i) if (p[i] != 0) return false; return true; }
bool Surface(const char* text) noexcept {
    if (text == nullptr || text[0] == 0) return false;
    for (unsigned i = 0; i < 32; ++i) {
        const char c = text[i];
        if (c == 0) return true;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
    }
    return false;
}
double Polynomial(const CalibrationParameters& p, double u) noexcept {
    double height = p.coefficients[9];
    for (int i = 8; i >= 0; --i) height = height * u + p.coefficients[i];
    return height + p.zOffset;
}
unsigned Binomial(unsigned n, unsigned k) noexcept {
    unsigned result = 1;
    for (unsigned i = 1; i <= k; ++i) result = result * (n + 1 - i) / i;
    return result;
}
bool Positive(const double* b, double margin) noexcept {
    // Explicit bounded DFS avoids recursive consumption of the main task's stack.
    struct Interval { double b[9]; unsigned depth; } pending[21];
    std::memcpy(pending[0].b,b,sizeof(pending[0].b)); pending[0].depth=0;
    unsigned count=1, budget=4096;
    while (count != 0) {
        if (budget-- == 0) return false;
        const Interval interval=pending[--count];
        double low=interval.b[0], high=interval.b[0];
        for (unsigned i=1; i<9; ++i) { if (interval.b[i]<low) low=interval.b[i]; if (interval.b[i]>high) high=interval.b[i]; }
        if (low>margin) continue;
        if (high<=margin || interval.depth==20 || count+2>21) return false;
        double work[9]; std::memcpy(work,interval.b,sizeof(work));
        Interval& right=pending[count++]; Interval& left=pending[count++];
        left.depth=right.depth=interval.depth+1; left.b[0]=work[0]; right.b[8]=work[8];
        for (unsigned level=1; level<9; ++level) {
            for (unsigned i=0; i<9-level; ++i) work[i]=(work[i]+work[i+1])*0.5;
            left.b[level]=work[0]; right.b[8-level]=work[8-level];
        }
    }
    return true;
}
bool Monotonic(const CalibrationParameters& p) noexcept {
    // Convert dP/du, u=2t-1, to degree-eight Bernstein coefficients on [0,1].
    double powers[9] = {}, b[9] = {};
    for (unsigned i = 0; i < 9; ++i) {
        for (unsigned k = i; k < 9; ++k) {
            const double term = (k + 1) * p.coefficients[k+1] * Binomial(k,i) * (1u << i);
            powers[i] += ((k-i) & 1) ? -term : term;
        }
        for (unsigned k = 0; k <= i; ++k) b[i] += powers[k] * Binomial(i,k) / Binomial(8,k);
    }
    double weighted=0, power=1;
    for (unsigned k=0;k<9;++k) { weighted+=(k+1)*std::fabs(p.coefficients[k+1])*power; power*=3; }
    const double margin=1e-9L+256*std::numeric_limits<double>::epsilon()*weighted;
    return Positive(b,margin);
}
}
uint32_t CalibrationModel::Crc32(const uint8_t* data, size_t length) noexcept {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    return ~crc;
}
ModelError CalibrationModel::Load(const uint8_t* bytes, size_t length) noexcept {
    if (bytes == nullptr || length != CalibrationFileSize || std::memcmp(bytes, "CARTOV3\0", 8) != 0 || UInt(bytes+8) != 1 || UInt(bytes+12) != CalibrationFileSize ||
        !Zero(bytes+22,2) || !Zero(bytes+100,4) || !Zero(bytes+304,12) || UInt(bytes+88) > 1) return ModelError::Format;
    if (Crc32(bytes,316) != UInt(bytes+316)) return ModelError::Integrity;
    CalibrationParameters p;
    std::memcpy(p.uuid,bytes+16,6); std::memcpy(p.profileHash,bytes+24,32); std::memcpy(p.surface,bytes+56,32);
    bool allFF = true; for (uint8_t value : p.uuid) allFF = allFF && value == 255;
    if (Zero(p.uuid,6) || allFF) return ModelError::Identity;
    if (Zero(p.profileHash,32)) return ModelError::Profile;
    if (!Surface(p.surface)) return ModelError::Surface;
    const size_t surfaceLength = std::strlen(p.surface);
    if (!Zero(bytes+56+surfaceLength,32-surfaceLength)) return ModelError::Surface;
    p.temperatureCompensation = UInt(bytes+88) != 0; p.trainingCount = UInt(bytes+92); p.validationCount = UInt(bytes+96);
    for (unsigned i = 0; i < 2; ++i) p.domain[i] = Number(bytes+104+8*i);
    for (unsigned i = 0; i < 10; ++i) p.coefficients[i] = Number(bytes+120+8*i);
    p.referenceTemperature = Number(bytes+200); p.temperatureMin = Number(bytes+208); p.temperatureMax = Number(bytes+216);
    p.zOffset = Number(bytes+224); p.heightMin = Number(bytes+232); p.heightMax = Number(bytes+240);
    p.validationMax = Number(bytes+248); p.validationRms = Number(bytes+256);
    for (unsigned i = 0; i < 5; ++i) p.compensation[i] = Number(bytes+264+8*i);
    if (!In(p.domain[0],1.0L/30000000,1.0L/1000) || !In(p.domain[1],1.0L/30000000,1.0L/1000) || p.domain[0] >= p.domain[1]) return ModelError::Domain;
    if (!In(p.temperatureMin,-40,150) || !In(p.temperatureMax,p.temperatureMin,150) || !In(p.referenceTemperature,p.temperatureMin,p.temperatureMax) ||
        (!p.temperatureCompensation && p.temperatureMax-p.temperatureMin > 1)) return ModelError::Temperature;
    for (double c : p.coefficients) if (!In(c,-1e6,1e6)) return ModelError::Coefficients;
    if (!In(p.zOffset,-10,10) || !In(p.heightMin,-10,100) || !In(p.heightMax,p.heightMin,100) || p.heightMin>=p.heightMax ||
        std::fabs(Polynomial(p,-1)-p.heightMin) > 1e-7L || std::fabs(Polynomial(p,1)-p.heightMax) > 1e-7L) return ModelError::Range;
    if (p.trainingCount < 10 || p.trainingCount > 100000 || p.validationCount < 10 || p.validationCount > 100000 ||
        !In(p.validationMax,0,0.001L) || !In(p.validationRms,0,p.validationMax)) return ModelError::Quality;
    if (!Monotonic(p)) return ModelError::Monotonic;
    if (p.temperatureCompensation) {
        for (double c : p.compensation) if (!std::isfinite(c)) return ModelError::Temperature;
        if (!In(p.compensation[4],1000,30000000)) return ModelError::Temperature;
        // Prove the quadratic denominator remains positive throughout the admitted temperatures.
        const double aa=p.compensation[0], ba=p.compensation[2];
        double minimum = std::fmin(std::pow(2*p.temperatureMin*aa+ba,2)+4*aa, std::pow(2*p.temperatureMax*aa+ba,2)+4*aa);
        if (aa != 0 && In(-ba/(2*aa),p.temperatureMin,p.temperatureMax)) minimum = std::fmin(minimum,4*aa);
        if (!std::isfinite(minimum) || minimum <= 1e-18L) return ModelError::Temperature;
    } else if (!Zero(bytes+264,40)) return ModelError::Temperature;
    parameters = p; loaded = true; return ModelError::None;
}
ModelError CalibrationModel::Compensate(const CalibrationParameters& p, double frequency, double source, double& result) noexcept {
    if (!In(frequency,1000,30000000) || !In(source,p.temperatureMin,p.temperatureMax)) return ModelError::Temperature;
    if (!p.temperatureCompensation) { result = frequency; return ModelError::None; }
    const double aa=p.compensation[0], ab=p.compensation[1], ba=p.compensation[2], bb=p.compensation[3], fmin=p.compensation[4];
    const double q=frequency-fmin, t=source, target=p.referenceTemperature;
    const double a=4*(t*aa)*(t*aa)+4*t*aa*ba+ba*ba+4*aa;
    const double b=8*t*t*aa*ab+4*t*(aa*bb+ab*ba)+2*ba*bb+4*ab-4*q*aa;
    const double c=4*(t*ab)*(t*ab)+4*t*ab*bb+bb*bb-4*q*ab;
    const double discriminant=b*b-4*a*c;
    if (!std::isfinite(discriminant) || !std::isfinite(a) || a <= 1e-18L) return ModelError::Temperature;
    if (discriminant < 0) {
        const double pa=aa*q+ab, pb=ba*q+bb;
        result=frequency-pa*t*t-pb*t+pa*target*target+pb*target;
    } else {
        const double ax=(std::sqrt(discriminant)-b)/(2*a), pa=aa*ax+ab, pb=ba*ax+bb;
        if (std::fabs(pa)>1e-12L) { const double offset=pb/(2*pa); result=pa*(target+offset)*(target+offset)+ax+fmin; }
        else result=pb*target+ax+fmin;
    }
    return In(result,1000,30000000) ? ModelError::None : ModelError::Temperature;
}
ModelError CalibrationModel::Evaluate(double frequency, double temperature, const uint8_t uuid[6], const uint8_t hash[32], const char* surface, double& height, double* corrected) const noexcept {
    height = 0;
    if (corrected != nullptr) *corrected = 0;
    if (!loaded) return ModelError::Missing;
    if (uuid == nullptr || std::memcmp(uuid,parameters.uuid,6) != 0) return ModelError::Identity;
    if (hash == nullptr || std::memcmp(hash,parameters.profileHash,32) != 0) return ModelError::Profile;
    if (!Surface(surface) || std::strcmp(surface,parameters.surface) != 0) return ModelError::Surface;
    double compensated = 0;
    const ModelError error = Compensate(parameters,frequency,temperature,compensated);
    if (error != ModelError::None) return error;
    const double reciprocal = 1/compensated;
    if (reciprocal < parameters.domain[0] || reciprocal > parameters.domain[1]) return ModelError::Domain;
    const double u=(2*reciprocal-parameters.domain[0]-parameters.domain[1])/(parameters.domain[1]-parameters.domain[0]);
    const double result=Polynomial(parameters,u);
    if (!In(result,parameters.heightMin-1e-9L,parameters.heightMax+1e-9L)) return ModelError::Range;
    height = result; if (corrected != nullptr) *corrected = compensated;
    return ModelError::None;
}
const char* CalibrationModel::ErrorName(ModelError error) noexcept {
    static const char* const names[]={"none","missing","format","integrity","uuid","profile","surface","domain","temperature","coefficients","monotonic","quality","range","stale"};
    return names[static_cast<unsigned>(error)];
}
}
