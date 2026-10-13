#include <cstdint>
#include <cstdio>
#include <cstdlib>

// Firmware's handler saves MCU registers in a naked function. On the host we
// retain the assertion failure and report its source location before aborting.
extern "C" [[noreturn]] void vAssertCalled(uint32_t line, const char* file) noexcept
{
    std::fprintf(stderr, "Firmware assertion failed: %s:%u\n", file, line);
    std::abort();
}
