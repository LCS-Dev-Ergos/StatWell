//===---------------------------------------------------------------------------===//
/**
 * @file linux_parsers.hpp
 * @author LCS.Dev - StatWell
 * @brief Bounded parsers for Linux procfs and sysfs metric records.
 * @version 0.1
 * @date 2026-09-28
 *
 * @copyright MIT License 2026
 */
//===---------------------------------------------------------------------------===//

#pragma once

#ifndef STATWELL_LINUX_PARSERS_HPP
#define STATWELL_LINUX_PARSERS_HPP

#include "statwell/metrics.hpp"

#include <string_view>

namespace statwell::detail {

/** @brief Parse the aggregate cpu line in /proc/stat. */
[[nodiscard]] Result<CpuTicks> parse_proc_stat(std::string_view text) noexcept;

struct MemoryRecords {
  std::string_view meminfo;
  std::string_view psi;
};

/** @brief Parse MemTotal and MemAvailable, with optional PSI pressure text. */
[[nodiscard]] Result<MemorySample> parse_meminfo(const MemoryRecords& records) noexcept;

/** @brief Parse received and sent bytes for an exact interface name. */
[[nodiscard]] Result<NetworkCounters> parse_net_dev(std::string_view text, std::string_view interface_name) noexcept;

struct BatteryRecords {
  std::string_view capacity;
  std::string_view status;
  bool external_power = false;
};

/** @brief Parse capacity and charge state from a sysfs battery. */
[[nodiscard]] Result<BatterySample> parse_battery(BatteryRecords records) noexcept;

} // namespace statwell::detail

#endif // STATWELL_LINUX_PARSERS_HPP

//===---------------------------------------------------------------------------===//
