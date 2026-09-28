//===---------------------------------------------------------------------------===//
/**
 * @file linux_parsers_test.cpp
 * @author LCS.Dev - StatWell
 * @brief Recorded procfs/sysfs fixtures and malformed-input parser tests.
 * @version 0.1
 * @date 2026-09-28
 *
 * @copyright MIT License 2026
 */
//===---------------------------------------------------------------------------===//

#include "linux_parsers.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <string>

namespace statwell::detail {
namespace {

[[nodiscard]] std::string fixture(const char* name) {
  const std::string path = std::string(STATWELL_SOURCE_DIR) + "/tests/fixtures/linux/" + name;
  std::ifstream     file(path);
  EXPECT_TRUE(file.is_open()) << path;
  return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

TEST(LinuxParsers, ReadsAggregateCpuWithoutDoubleCountingGuestTicks) {
  const auto result = parse_proc_stat(fixture("proc_stat.txt"));
  ASSERT_TRUE(result);
  EXPECT_EQ(result->user, 100);
  EXPECT_EQ(result->nice, 10);
  EXPECT_EQ(result->system, 25);
  EXPECT_EQ(result->idle, 206);
}

TEST(LinuxParsers, RejectsMalformedAndOverflowingCpuCounters) {
  EXPECT_EQ(parse_proc_stat("cpu 1 2 3\n").error().code, ErrorCode::invalid_input);
  EXPECT_EQ(parse_proc_stat("cpu 1 2 3 4 0 18446744073709551615 1\n").error().code, ErrorCode::invalid_input);
}

TEST(LinuxParsers, ReadsMemoryAndPsiPressure) {
  const auto result = parse_meminfo({fixture("meminfo.txt"), fixture("pressure_memory.txt")});
  ASSERT_TRUE(result);
  EXPECT_EQ(result->total_bytes, 16'384'000ULL * 1'024);
  EXPECT_EQ(result->available_bytes, 4'096'000ULL * 1'024);
  EXPECT_EQ(result->used_bytes, 12'288'000ULL * 1'024);
  EXPECT_EQ(result->pressure, Pressure::warning);
}

TEST(LinuxParsers, DegradesWhenPsiIsMissingAndRejectsBadMemory) {
  const auto result = parse_meminfo({fixture("meminfo.txt"), ""});
  ASSERT_TRUE(result);
  EXPECT_EQ(result->pressure, Pressure::unknown);
  EXPECT_EQ(parse_meminfo({"MemTotal: 100 kB\nMemAvailable: 101 kB\n", ""}).error().code, ErrorCode::invalid_input);
  EXPECT_EQ(parse_meminfo({"MemTotal: 18446744073709551615 kB\nMemAvailable: 1 kB\n", ""}).error().code, ErrorCode::invalid_input);
  EXPECT_EQ(parse_meminfo({"MemTotal: 100 kB\nMemTotal: 100 kB\nMemAvailable: 50 kB\n", ""}).error().code, ErrorCode::invalid_input);
  const auto critical = parse_meminfo({fixture("meminfo.txt"), "some avg10=10.00 avg60=0.00\n"});
  ASSERT_TRUE(critical);
  EXPECT_EQ(critical->pressure, Pressure::critical);
}

TEST(LinuxParsers, ReadsNamedInterfaceAndRejectsTruncatedRecords) {
  const auto result = parse_net_dev(fixture("net_dev.txt"), "eth0");
  ASSERT_TRUE(result);
  EXPECT_EQ(result->received_bytes, 123'456);
  EXPECT_EQ(result->sent_bytes, 654'321);
  EXPECT_EQ(parse_net_dev(fixture("net_dev.txt"), "wlan0").error().code, ErrorCode::unavailable);
  EXPECT_EQ(parse_net_dev(" eth0: 100 1\n", "eth0").error().code, ErrorCode::invalid_input);
  EXPECT_EQ(parse_net_dev(" eth0: 18446744073709551616 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0\n", "eth0").error().code, ErrorCode::invalid_input);
}

TEST(LinuxParsers, ReadsBatteryAndRejectsInvalidCapacity) {
  const auto result = parse_battery({fixture("battery_capacity.txt"), fixture("battery_status.txt"), true});
  ASSERT_TRUE(result);
  EXPECT_EQ(result->percent, 87);
  EXPECT_TRUE(result->charging);
  EXPECT_TRUE(result->external_power);
  EXPECT_EQ(parse_battery({"101\n", "Charging\n", false}).error().code, ErrorCode::invalid_input);
  const auto full = parse_battery({"100\n", "Full\n", true});
  ASSERT_TRUE(full);
  EXPECT_FALSE(full->charging);
}

} // namespace
} // namespace statwell::detail

//===---------------------------------------------------------------------------===//
