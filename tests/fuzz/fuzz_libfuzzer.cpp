// libFuzzer entry point (clang only, -DPKTOOL_FUZZ=ON). One executable per
// target: PKTOOL_FUZZ_TARGET is the target name, e.g. "bank".
#include <cstddef>
#include <cstdint>

#include "tests/fuzz/fuzz_targets.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    static const pktool::fuzz::Target* target = pktool::fuzz::findTarget(PKTOOL_FUZZ_TARGET);
    if (target) target->fn(data, size);
    return 0;
}
