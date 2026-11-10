#include "check.h"

namespace check {
std::vector<Test> &tests()
{
    static std::vector<Test> t;
    return t;
}
int failures;
} // namespace check

int main()
{
    int failed_tests = 0;
    for (const check::Test &t : check::tests()) {
        int before = check::failures;
        t.fn();
        bool ok = check::failures == before;
        std::printf("%s %s\n", ok ? "ok  " : "FAIL", t.name);
        failed_tests += !ok;
    }
    std::printf("%zu tests, %d failed\n", check::tests().size(), failed_tests);
    return failed_tests ? 1 : 0;
}
