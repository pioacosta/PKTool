// Edge coverage for the self-contained driver, for compilers without
// libFuzzer: the code under test is built with -fsanitize-coverage=trace-pc,
// which calls this function at every basic block. This file itself is built
// without that flag.
#include <cstdint>

extern "C" {
uint8_t pktool_cov_map[1 << 16];
static uint32_t g_prev = 0;

void __sanitizer_cov_trace_pc(void) {
    uint32_t cur = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(__builtin_return_address(0)) >> 1);
    cur = (cur * 2654435761u) >> 16;
    uint32_t index = (cur ^ g_prev) & 0xFFFF;
    if (pktool_cov_map[index] < 255) ++pktool_cov_map[index];
    g_prev = cur >> 1;
}
}
