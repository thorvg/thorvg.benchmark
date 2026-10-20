#pragma once

#include <cmath>
#include <iostream>

inline int test_checks = 0;
inline int test_failures = 0;

inline void test_check(bool passed, const char* expression, const char* file, int line)
{
    ++test_checks;
    if (!passed) {
        ++test_failures;
        std::cerr << file << ':' << line << ": " << expression << '\n';
    }
}

#define CHECK(...) test_check(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__, __FILE__, __LINE__)
#define CHECK_NEAR(actual, expected, tolerance) \
    CHECK(std::abs((actual) - (expected)) <= (tolerance))

inline int test_result()
{
    std::cout << test_checks << " checks, " << test_failures << " failures\n";
    return test_failures || test_checks == 0 ? 1 : 0;
}
