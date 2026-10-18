// Cartographer3D scan_model.py / temperature_compensation.py, GPLv3; see README.md.
#pragma once
#include <cstddef>
#include <cstdint>

namespace RrfModules::CartographerV3 {
constexpr size_t CalibrationFileSize = 320;
enum class ModelError : uint8_t { None, Missing, Format, Integrity, Identity, Profile, Surface, Domain, Temperature, Coefficients, Monotonic, Quality, Range, Stale };
struct CalibrationParameters {
    uint8_t uuid[6] = {}, profileHash[32] = {};
    char surface[32] = {};
    bool temperatureCompensation = false;
    uint32_t trainingCount = 0, validationCount = 0;
    double domain[2] = {}, coefficients[10] = {};
    double referenceTemperature = 0, temperatureMin = 0, temperatureMax = 0, zOffset = 0;
    double heightMin = 0, heightMax = 0, validationMax = 0, validationRms = 0;
    double compensation[5] = {}; // a_a, a_b, b_a, b_b, fmin. No implicit factory calibration.
};
class CalibrationModel {
public:
    // Transactional: an unsuccessful load leaves the preceding verified model intact.
    ModelError Load(const uint8_t* bytes, size_t length) noexcept;
    void Clear() noexcept { loaded = false; }
    bool Loaded() const noexcept { return loaded; }
    const CalibrationParameters& Parameters() const noexcept { return parameters; }
    ModelError Evaluate(double frequency, double temperature, const uint8_t uuid[6], const uint8_t profileHash[32], const char* surface, double& height, double* compensatedFrequency = nullptr) const noexcept;
    static ModelError Compensate(const CalibrationParameters& p, double frequency, double sourceTemperature, double& result) noexcept;
    static uint32_t Crc32(const uint8_t* data, size_t length) noexcept;
    static const char* ErrorName(ModelError error) noexcept;
private:
    CalibrationParameters parameters;
    bool loaded = false;
};
}
