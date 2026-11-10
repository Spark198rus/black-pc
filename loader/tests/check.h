// A minimal test runner: TEST(name) { CHECK(cond); CHECK_EQ(a, b); }. No dependencies.
#pragma once

#include <cstdio>
#include <functional>
#include <vector>

namespace check {

struct Test {
    const char *name;
    std::function<void()> fn;
};
std::vector<Test> &tests();
extern int failures;

struct Register {
    Register(const char *name, std::function<void()> fn) { tests().push_back({name, std::move(fn)}); }
};

inline bool report(bool ok, const char *expr, const char *file, int line)
{
    if (!ok) {
        std::printf("  FAILED %s(%d): %s\n", file, line, expr);
        failures++;
    }
    return ok;
}

} // namespace check

#define CHECK_CAT2(a, b) a##b
#define CHECK_CAT(a, b) CHECK_CAT2(a, b)
#define TEST(name)                                                              \
    static void name();                                                         \
    static check::Register CHECK_CAT(reg_, name)(#name, name);                  \
    static void name()
#define CHECK(cond) check::report((cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) check::report((a) == (b), #a " == " #b, __FILE__, __LINE__)
// Stops the test when the check fails (later checks would read past the end, say).
#define REQUIRE(cond) \
    if (!CHECK(cond)) return
