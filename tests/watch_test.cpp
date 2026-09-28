//===---------------------------------------------------------------------------===//
/**
 * @file watch_test.cpp
 * @author LCS.Dev - StatWell
 * @brief Regression checks for unchanged SketchyBar samples that expire.
 * @version 0.1
 * @date 2026-09-28
 *
 * @copyright MIT License 2026
 */
//===---------------------------------------------------------------------------===//

#include "watch_state.hpp"

#include <gtest/gtest.h>

namespace statwell::detail {
namespace {

TEST(Watch, EmitsOneExpiryEventForAnUnchangedSample) {
  const auto initial = event_identity("daemon", "sequence=4", 10'000, 6'000, 16'000);
  EXPECT_EQ(initial, event_identity("daemon", "sequence=4", 10'000, 6'000, 17'000 - 1));
  const auto expired = event_identity("daemon", "sequence=4", 10'000, 6'000, 17'000);
  EXPECT_NE(initial, expired);
  EXPECT_EQ(expired, event_identity("daemon", "sequence=4", 10'000, 6'000, 18'000));
  EXPECT_NE(expired, event_identity("daemon", "sequence=5", 12'000, 6'000, 18'000));
}

TEST(Watch, IgnoresMissingTimestampsAndHandlesLargeAges) {
  EXPECT_EQ(event_identity("daemon", "sequence=1", 0, 6'000, 20'000), event_identity("daemon", "sequence=1", 0, 6'000, 21'000));
  EXPECT_EQ(
      event_identity("daemon", "sequence=1", 1, INT64_MAX, INT64_MAX), event_identity("daemon", "sequence=1", 1, INT64_MAX, INT64_MAX - 1));
}

} // namespace
} // namespace statwell::detail

//===---------------------------------------------------------------------------===//
