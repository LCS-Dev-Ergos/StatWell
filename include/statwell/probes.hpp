//===---------------------------------------------------------------------------===//
/**
 * @file probes.hpp
 * @author LCS.Dev - StatWell
 * @brief Narrow native probe interface and stateful rate samplers.
 * @version 0.1
 * @date 2026-09-28
 *
 * @copyright MIT License 2026
 */
//===---------------------------------------------------------------------------===//

#pragma once

#ifndef STATWELL_PROBES_HPP
#define STATWELL_PROBES_HPP

#include "statwell/metrics.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace statwell {

//===----- INSTANTANEOUS PLATFORM READINGS ------------------------------------===//

/** @brief Reads cumulative CPU ticks across all processors. */
[[nodiscard]] Result<CpuTicks> read_cpu_ticks() noexcept;

/** @brief Samples physical memory use and pressure. */
[[nodiscard]] Result<MemorySample> sample_memory() noexcept;

/** @brief Samples system load averages. */
[[nodiscard]] Result<LoadSample> sample_load() noexcept;

/** @brief Samples the filesystem containing path. */
[[nodiscard]] Result<DiskSample> sample_disk(const std::string& path) noexcept;

/** @brief Samples the internal battery, if present. */
[[nodiscard]] Result<BatterySample> sample_battery() noexcept;

/** @brief Reads cumulative counters for a named, active interface. */
[[nodiscard]] Result<NetworkCounters> read_network_counters(std::string_view interface_name) noexcept;

//===----- RATE SAMPLERS ------------------------------------------------------===//

/** @brief Computes CPU utilization from successive native counter reads. */
class CpuProbe {
public:
  /** @brief Returns a rate after the first read has established a baseline. */
  [[nodiscard]] Result<CpuSample> sample() noexcept;

private:
  std::optional<CpuTicks> previous_;
};

/** @brief Computes throughput for one named network interface. */
class NetworkProbe {
public:
  /** @brief Selects the interface whose counters will be sampled. */
  explicit NetworkProbe(std::string interface_name);

  /** @brief Returns a rate after the first read has established a baseline. */
  [[nodiscard]] Result<NetworkSample> sample() noexcept;

private:
  std::string                           interface_name_;
  std::optional<NetworkCounters>        previous_;
  std::chrono::steady_clock::time_point previous_time_{};
};

} // namespace statwell

#endif // STATWELL_PROBES_HPP

//===---------------------------------------------------------------------------===//
