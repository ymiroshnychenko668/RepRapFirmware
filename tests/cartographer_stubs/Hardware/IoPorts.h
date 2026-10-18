#pragma once
#include <RepRapFirmware.h>
inline bool fakePinConflict = false;
inline bool fakeWrongAlias = false;
inline unsigned fakeOwnedPins = 0;
constexpr unsigned PD_0 = 48, PD_1 = 49;
struct IoPort {
    bool owns = false;
    unsigned pin = 0;
    bool AssignPort(const char* name, const StringRef&, PinUsedBy, PinAccess) noexcept {
        if (fakePinConflict) return false;
        if (!owns) ++fakeOwnedPins;
        pin = fakeWrongAlias ? 0 : (std::string(name) == "d.0" ? PD_0 : PD_1);
        owns = true; return true;
    }
    unsigned GetPin() const noexcept { return pin; }
    bool GetTotalInvert() const noexcept { return false; }
    void Release() noexcept { if (owns) --fakeOwnedPins; owns = false; }
};
