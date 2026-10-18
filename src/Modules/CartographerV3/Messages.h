#pragma once
namespace RrfModules::CartographerV3 {
// Authored command errors: English and Ukrainian only. Diagnostic field names are protocol data.
enum class Message { Uuid, Pins, Active, Parameters, Language, ModelParameters, ModelFile, ModelInvalid };
inline const char* Text(Message message, bool ukrainian) noexcept {
    static const char* const en[] = {
        "Cartographer: U must contain exactly 12 hexadecimal UUID digits",
        "Cartographer: CAN pins PD0/PD1 are unavailable",
        "Cartographer: stop the active session before starting another",
        "Cartographer: S must be 0, 1, 2 or 3",
        "Cartographer: L must be en or uk",
        "Cartographer: model loading requires P filename, U UUID and B surface ID",
        "Cartographer: cannot read the complete calibration file",
        "Cartographer: calibration model rejected"
    };
    static const char* const uk[] = {
        "Cartographer: U має містити рівно 12 шістнадцяткових цифр UUID",
        "Cartographer: контакти CAN PD0/PD1 недоступні",
        "Cartographer: зупиніть активний сеанс перед запуском іншого",
        "Cartographer: S має бути 0, 1, 2 або 3",
        "Cartographer: L має бути en або uk",
        "Cartographer: для моделі потрібні P файл, U UUID та B ідентифікатор поверхні",
        "Cartographer: не вдалося прочитати повний файл калібрування",
        "Cartographer: модель калібрування відхилено"
    };
    return (ukrainian ? uk : en)[static_cast<unsigned>(message)];
}
}
