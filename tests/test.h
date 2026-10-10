#pragma once

// Minimal self-registering test harness (no external dependencies, works
// with -fno-exceptions).

#include <cstdio>
#include <vector>

namespace pktool::test {

struct TestCase {
    const char* name;
    void (*fn)();
};

std::vector<TestCase>& registry();
void reportFailure(const char* file, int line, const char* expr);

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

}  // namespace pktool::test

#define PKTOOL_CONCAT_INNER(a, b) a##b
#define PKTOOL_CONCAT(a, b) PKTOOL_CONCAT_INNER(a, b)

#define TEST(name)                                                                      \
    static void name();                                                                 \
    static ::pktool::test::Registrar PKTOOL_CONCAT(registrar_, name)(#name, &name);     \
    static void name()

#define CHECK(expr)                                                        \
    do {                                                                   \
        if (!(expr)) ::pktool::test::reportFailure(__FILE__, __LINE__, #expr); \
    } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))
