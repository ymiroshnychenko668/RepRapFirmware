#pragma once
namespace RrfModules::CartographerV3 {
// Authored command errors: English and Ukrainian only. Diagnostic field names are protocol data.
enum class Message { Uuid, Pins, Active, Parameters, Language, ModelParameters, ModelFile, ModelInvalid, Busy, Settings, Trigger, Prepare, NotReady, Wait, Session, Capture, Discovery };
inline const char* Text(Message message, bool ukrainian) noexcept {
    static const char* const en[] = {
        "U: expected 12 hexadecimal UUID digits",
        "CAN PD0/PD1 unavailable",
        "Stop the active Cartographer session first",
        "Unsupported Cartographer S action",
        "L: use en or uk",
        "Model requires P file, U UUID and B surface",
        "Cannot read complete calibration file",
        "Calibration model rejected",
        "Finish probing/scanning before configuration",
        "Provide T/E/V/D; Touch requires valid R",
        "Trigger unavailable: start session with H1",
        "Probe preparation failed",
        "Fresh calibrated reading unavailable",
        "Session readiness timed out",
        "Start a Cartographer session first",
        "Capture requires verified profile and finite data",
        "Discovery requires idle CAN"
    };
    static const char* const uk[] = {
        "U: потрібно 12 шістнадцяткових цифр UUID",
        "CAN PD0/PD1 недоступні",
        "Спочатку зупиніть сеанс Cartographer",
        "Непідтримувана дія Cartographer S",
        "L: задайте en або uk",
        "Модель: потрібні P файл, U UUID, B поверхня",
        "Не вдалося прочитати весь файл калібрування",
        "Модель калібрування відхилено",
        "Завершіть зондування/сканування перед налаштуванням",
        "Задайте T/E/V/D; для Touch потрібен поріг R",
        "Тригер недоступний: запустіть сеанс з H1",
        "Підготовка зондування не вдалася",
        "Свіжа відкалібрована висота недоступна",
        "Час очікування готовності вичерпано",
        "Спочатку запустіть сеанс Cartographer",
        "Запис: потрібні перевірений профіль і скінченні дані",
        "Пошук потребує вільного CAN"
    };
    return (ukrainian ? uk : en)[static_cast<unsigned>(message)];
}
}
