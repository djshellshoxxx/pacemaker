#include "TestFramework.h"
#include <cstring>

int main(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failedCases = 0;
    for (const auto& c : pmtest::registry()) {
        if (filter && std::strstr(c.name, filter) == nullptr) continue;
        std::printf("[ RUN  ] %s\n", c.name);
        const int before = pmtest::failures();
        try { c.fn(); }
        catch (const pmtest::RequireFailed&) {}
        catch (const std::exception& e) { pmtest::report(false, "exception", __FILE__, __LINE__, e.what()); }
        ++run;
        const bool ok = pmtest::failures() == before;
        if (!ok) ++failedCases;
        std::printf("[ %s ] %s\n", ok ? " OK " : "FAIL", c.name);
    }
    std::printf("%d test cases, %d failed, %d failed checks\n", run, failedCases, pmtest::failures());
    return failedCases == 0 ? 0 : 1;
}
