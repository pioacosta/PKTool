// The fuzz executables do not link the test runner; fake_saves.h only needs
// CHECK() to abort loudly.
#include <cstdio>
#include <cstdlib>

#include "tests/test.h"

namespace pktool::test {

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

void reportFailure(const char* file, int line, const char* expr) {
    std::fprintf(stderr, "CHECK failed %s:%d: %s\n", file, line, expr);
    std::abort();
}

}  // namespace pktool::test
