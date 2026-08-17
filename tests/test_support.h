//
// a very small test harness.
//
// each test is its own executable with its own main(), so there is no
// registry, no fixtures and no framework to learn. a check records a
// pass or a failure and keeps going, because knowing that four things
// broke is more useful than stopping at the first.
//
// GPL 3.0 License (see: LICENSE)
// Copyright (C) 2026 tomaz stih
//

#ifndef TESTS_TEST_SUPPORT_H
#define TESTS_TEST_SUPPORT_H

#include <cstdio>
#include <string>

namespace test {

//
// running totals for the current executable.
//
inline int checks_run = 0;
inline int checks_failed = 0;
inline const char *current_section = "";

//
// Start a named group of checks. purely for readable output.
//
inline void section(const char *title)
{
    current_section = title;
    std::printf("\n-- %s\n", title);
}

//
// Record the outcome of one check.
//
inline void report(bool passed, const std::string &what,
                   const std::string &detail)
{
    ++checks_run;
    if (passed) {
        std::printf("   ok   %s\n", what.c_str());
        return;
    }

    ++checks_failed;
    std::printf("   FAIL %s\n        %s\n", what.c_str(),
                detail.c_str());
}

//
// Check a condition.
//
inline void check(bool condition, const std::string &what)
{
    report(condition, what, "expected the condition to hold");
}

//
// Check two integers for equality.
//
inline void check_eq(long long got, long long want,
                     const std::string &what)
{
    report(got == want, what,
           "got " + std::to_string(got) + ", wanted " +
               std::to_string(want));
}

//
// Check two strings for equality.
//
inline void check_eq_str(const std::string &got, const std::string &want,
                         const std::string &what)
{
    report(got == want, what, "got \"" + got + "\", wanted \"" + want +
                                  "\"");
}

//
// Check that a string contains a substring.
//
inline void check_contains(const std::string &haystack,
                           const std::string &needle,
                           const std::string &what)
{
    report(haystack.find(needle) != std::string::npos, what,
           "\"" + needle + "\" does not appear in \"" + haystack + "\"");
}

//
// Print the totals and return the process exit code.
//
// Returns:
//      0 when everything passed, 1 otherwise.
//
inline int summary(const char *suite)
{
    std::printf("\n%s: %d checks, %d failed\n", suite, checks_run,
                checks_failed);
    return checks_failed == 0 ? 0 : 1;
}

} // namespace test

#endif // TESTS_TEST_SUPPORT_H
