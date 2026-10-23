#pragma once

// Minimal test helpers: no external framework, each suite is a plain executable
// whose exit code is read by ctest.

#include <iostream>

namespace lumilink_test {

inline int& Failures()
{
  static int failures = 0;
  return failures;
}

inline int Finish(const char* suite)
{
  if (Failures() == 0) {
    std::cout << "[PASS] " << suite << std::endl;
    return 0;
  }
  std::cerr << "[FAIL] " << suite << ": " << Failures() << " check(s) failed" << std::endl;
  return 1;
}

}  // namespace lumilink_test

#define CHECK(expr)                                                                   \
  do {                                                                                \
    if (!(expr)) {                                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #expr << std::endl; \
      ++lumilink_test::Failures();                                                    \
    }                                                                                 \
  } while (0)

#define CHECK_EQ(actual, expected)                                                        \
  do {                                                                                    \
    const auto& checkActual_ = (actual);                                                  \
    const auto& checkExpected_ = (expected);                                              \
    if (!(checkActual_ == checkExpected_)) {                                              \
      std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_EQ failed: " #actual " == " #expected \
                << std::endl;                                                             \
      ++lumilink_test::Failures();                                                        \
    }                                                                                     \
  } while (0)

#define CHECK_THROWS(expr, exception_type)                                                   \
  do {                                                                                       \
    bool checkThrown_ = false;                                                               \
    try {                                                                                    \
      (void)(expr);                                                                          \
    }                                                                                        \
    catch (const exception_type&) {                                                          \
      checkThrown_ = true;                                                                   \
    }                                                                                        \
    catch (...) {                                                                            \
    }                                                                                        \
    if (!checkThrown_) {                                                                     \
      std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_THROWS failed: " #expr << std::endl; \
      ++lumilink_test::Failures();                                                           \
    }                                                                                        \
  } while (0)
