#pragma once
#include <RepRapFirmware.h>
#include "MessageType.h"
inline std::string fakeDiagnostics;
struct FakePlatform {
    void MessageF(MessageType, const char* format, ...) {
        char text[2048]; va_list args; va_start(args,format); vsnprintf(text,sizeof(text),format,args); va_end(args);
        fakeDiagnostics = text;
    }
};
