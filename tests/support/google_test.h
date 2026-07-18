#ifndef APGAR_TESTS_SUPPORT_GOOGLE_TEST_H_
#define APGAR_TESTS_SUPPORT_GOOGLE_TEST_H_

// GoogleTest 1.17 triggers this Clang warning from its own C++20 header. Keep
// the compatibility suppression scoped to the third-party include so APGAR
// test code still builds with every warning promoted to an error.
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcharacter-conversion"
#endif

#include <gtest/gtest.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif  // APGAR_TESTS_SUPPORT_GOOGLE_TEST_H_
