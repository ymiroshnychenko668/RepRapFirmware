#pragma once
#include <RepRapFirmware.h>
inline bool fakePinConflict = false;
inline unsigned fakeOwnedPins = 0;
struct IoPort {
    bool owns = false;
    bool AssignPort(const char*, const StringRef&, PinUsedBy, PinAccess) noexcept {
        if (fakePinConflict) return false;
        if (!owns) ++fakeOwnedPins;
        owns = true; return true;
    }
    void Release() noexcept { if (owns) --fakeOwnedPins; owns = false; }
};
