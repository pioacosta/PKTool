#include <cstring>

#include "tests/test.h"

namespace pktool::test {
namespace {
int g_failures = 0;
}

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

void reportFailure(const char* file, int line, const char* expr) {
    ++g_failures;
    std::printf("  FAILED %s:%d: %s\n", file, line, expr);
}

}  // namespace pktool::test

// Usage: pktool_tests [name-filter]
int main(int argc, char** argv) {
    using namespace pktool::test;
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    int failedTests = 0;
    for (const TestCase& t : registry()) {
        if (filter && std::strstr(t.name, filter) == nullptr) {
            continue;
        }
        int before = g_failures;
        t.fn();
        ++run;
        bool ok = g_failures == before;
        failedTests += ok ? 0 : 1;
        std::printf("[%s] %s\n", ok ? " OK " : "FAIL", t.name);
    }
    std::printf("\n%d test(s), %d failed\n", run, failedTests);
    return failedTests == 0 ? 0 : 1;
}
