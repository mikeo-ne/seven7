#pragma once
// seven7 — minimal zero-dependency test harness.
// Each test binary owns its counters (single translation unit per binary).

#include <cmath>
#include <cstdio>

namespace s7::test {

inline int g_checks = 0;
inline int g_failures = 0;

inline void record(bool ok, const char* expr, const char* file, int line) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("FAIL %s:%d  CHECK(%s)\n", file, line, expr);
  }
}

inline void record_near(double a, double b, double tol, const char* ea, const char* eb,
                        const char* file, int line) {
  ++g_checks;
  if (!(std::fabs(a - b) <= tol)) {
    ++g_failures;
    std::printf("FAIL %s:%d  CHECK_NEAR(%s, %s): %f vs %f (tol %f)\n", file, line, ea, eb, a,
                b, tol);
  }
}

inline int summary(const char* suite) {
  std::printf("%s: %d checks, %d failure(s)\n", suite, g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}

} // namespace s7::test

#define S7_CHECK(cond) ::s7::test::record(static_cast<bool>(cond), #cond, __FILE__, __LINE__)
#define S7_CHECK_NEAR(a, b, tol) \
  ::s7::test::record_near((a), (b), (tol), #a, #b, __FILE__, __LINE__)
