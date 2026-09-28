//===---------------------------------------------------------------------------===//
/**
 * @file metrics_test.cpp
 * @author LCS.Dev - StatWell
 * @brief Boundary tests for platform-independent metric calculations.
 * @version 0.1
 * @date 2026-09-28
 *
 * @copyright MIT License 2026
 */
//===---------------------------------------------------------------------------===//

#include "statwell/metrics.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace statwell {
namespace {

TEST(Cpu, ComputesPercentagesFromCounterDeltas) {
  const auto result = derive_cpu(CpuTicks{100, 100, 800, 0}, CpuTicks{130, 120, 850, 0});
  ASSERT_TRUE(result.has_value());
  EXPECT_DOUBLE_EQ(result->user_percent, 30.0);
  EXPECT_DOUBLE_EQ(result->system_percent, 20.0);
  EXPECT_DOUBLE_EQ(result->total_percent, 50.0);
}

TEST(Cpu, RejectsResetAndZeroElapsedCounters) {
  EXPECT_EQ(derive_cpu(CpuTicks{10, 0, 0, 0}, CpuTicks{0, 0, 0, 0}).error().code, ErrorCode::unavailable);
  EXPECT_EQ(derive_cpu(CpuTicks{1, 1, 1, 1}, CpuTicks{1, 1, 1, 1}).error().code, ErrorCode::unavailable);
}

TEST(Memory, UsesAnonymousWiredAndCompressedPages) {
  // Fixture shape mirrors macOS vm_statistics64 and hw.memsize values.
  const auto result = derive_memory(MemoryPages{1'000, 10, 30, 5, 10, 5, Pressure::warning});
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->used_bytes, 400U);
  EXPECT_EQ(result->available_bytes, 600U);
  EXPECT_EQ(result->pressure, Pressure::warning);
}

TEST(Memory, RejectsImpossiblePageCounts) {
  EXPECT_EQ(derive_memory(MemoryPages{100, 10, 1, 2, 0, 0, Pressure::unknown}).error().code, ErrorCode::invalid_input);
}

TEST(Disk, RejectsOverflow) {
  EXPECT_EQ(derive_disk(std::numeric_limits<std::uint64_t>::max(), 1, 2).error().code, ErrorCode::invalid_input);
}

TEST(Network, ReportsBytesPerSecondAndRejectsReset) {
  const auto result = derive_network(NetworkCounters{100, 200}, NetworkCounters{300, 500}, 2'000'000'000);
  ASSERT_TRUE(result.has_value());
  EXPECT_DOUBLE_EQ(result->download_bytes_per_second, 100.0);
  EXPECT_DOUBLE_EQ(result->upload_bytes_per_second, 150.0);
  EXPECT_EQ(derive_network(NetworkCounters{500, 200}, NetworkCounters{300, 500}, 2'000'000'000).error().code, ErrorCode::unavailable);
}

} // namespace
} // namespace statwell

//===---------------------------------------------------------------------------===//
