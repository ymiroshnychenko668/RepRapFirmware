#pragma once
#include "CartographerV3.h"
namespace RrfModules::CartographerV3 {
enum class ProbeMessage { Configuration, Unavailable, ExternalModel, Failed, HomingAxis, Away, MapFile };
inline const char* ProbeText(ProbeMessage message) noexcept {
    static const char* const en[] = {
        "Use M558 P12 C\"cartographer-v3\"",
        "Cartographer height unavailable",
        "Use M558.9 S2/S6",
        "Cartographer probing failed",
        "Home Z downwards",
        "Probe downwards on Z",
        "Choose another height map file"
    };
    static const char* const uk[] = {
        "Вкажіть M558 P12 C\"cartographer-v3\"",
        "Висота Cartographer недоступна",
        "Вкажіть M558.9 S2/S6",
        "Помилка зондування Cartographer",
        "Шукайте нуль Z вниз",
        "Зондуйте Z вниз",
        "Вкажіть інший файл карти висот"
    };
    return (IsUkrainian() ? uk : en)[static_cast<unsigned>(message)];
}
}
