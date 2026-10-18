#pragma once
// Keep the dispatch gate shared by production G-code handling and its host test.
inline bool SupportsFractionalMCode(unsigned int code) noexcept
{
    switch (code)
    {
    case 36: case 201: case 260: case 261: case 505:
    case 569: case 586: case 587:
        return true;
#if SUPPORT_SCANNING_PROBES || RRF_MODULE_CARTOGRAPHER_V3
    case 558: return true;
#endif
#if SUPPORT_PHASE_STEPPING
    case 970: return true;
#endif
    default: return false;
    }
}
