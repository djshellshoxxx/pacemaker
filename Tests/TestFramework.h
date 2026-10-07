// Tiny self-contained test framework: a registry plus CHECK/REQUIRE macros.
#pragma once
#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace pmtest {

struct Case { const char* name; void (*fn)(); };
inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int& failures() { static int f = 0; return f; }
struct Reg { Reg(const char* n, void (*f)()) { registry().push_back({ n, f }); } };
struct RequireFailed : std::runtime_error { using std::runtime_error::runtime_error; };

inline void report(bool ok, const char* expr, const char* file, int line, const std::string& msg)
{
    if (ok) return;
    ++failures();
    std::printf("  FAILED %s:%d: %s%s%s\n", file, line, expr, msg.empty() ? "" : "  -- ", msg.c_str());
}

} // namespace pmtest

#define TEST_CASE(name) \
    static void name(); \
    static pmtest::Reg reg_##name(#name, name); \
    static void name()

#define CHECK(cond) pmtest::report((cond), #cond, __FILE__, __LINE__, "")
#define CHECK_MSG(cond, msg) pmtest::report((cond), #cond, __FILE__, __LINE__, (msg))
#define REQUIRE(cond) do { if (!(cond)) { pmtest::report(false, #cond, __FILE__, __LINE__, "(required)"); \
    throw pmtest::RequireFailed(#cond); } } while (0)
#define REQUIRE_MSG(cond, msg) do { if (!(cond)) { pmtest::report(false, #cond, __FILE__, __LINE__, (msg)); \
    throw pmtest::RequireFailed(#cond); } } while (0)
