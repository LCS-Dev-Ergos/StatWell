//===---------------------------------------------------------------------------===//
/**
 * @file metrics.hpp
 * @author StatWell contributors
 * @brief Typed system metrics and platform-independent calculations.
 * @version 0.1
 * @date 2026-09-28
 *
 * @copyright MIT License 2026
 */
//===---------------------------------------------------------------------------===//

#pragma once

#ifndef STATWELL_METRICS_HPP
#define STATWELL_METRICS_HPP

#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>

namespace statwell {

/** @brief Stable error categories shared by all platform probes. */
enum class ErrorCode : std::uint8_t {
  unavailable,
  unsupported,
  invalid_input,
  system_failure,
};

/** @brief Probe failure with an optional native OS error code. */
struct ProbeError {
  ErrorCode code        = ErrorCode::unavailable;
  int       native_code = 0;
};

/** @brief Successful metric value or explicit probe failure. */
template <typename T>
using Result = std::expected<T, ProbeError>;

/** @brief Returns the stable string representation of an error category. */
[[nodiscard]] std::string_view error_name(ErrorCode code) noexcept;

/** @brief Monotonic CPU counters grouped by processor state. */
struct CpuTicks {
  std::uint64_t user;
  std::uint64_t system;
  std::uint64_t idle;
  std::uint64_t nice;
};

/** @brief CPU utilization over the interval between two counter reads. */
struct CpuSample {
  double user_percent;
  double system_percent;
  double total_percent;
};

/** @brief Derives CPU percentages; rejects counter rollback or an empty interval. */
[[nodiscard]] Result<CpuSample> derive_cpu(const CpuTicks& before, const CpuTicks& after) noexcept;

/** @brief Pressure level reported by the operating system, when available. */
enum class Pressure : std::uint8_t { unknown, normal, warning, critical };

/** @brief Page counts and size supplied by a platform memory backend. */
struct MemoryPages {
  std::uint64_t total_bytes;
  std::uint64_t page_size;
  std::uint64_t internal;
  std::uint64_t purgeable;
  std::uint64_t wired;
  std::uint64_t compressor;
  Pressure      pressure;
};

/** @brief Memory capacity and used/available byte counts. */
struct MemorySample {
  std::uint64_t total_bytes;
  std::uint64_t used_bytes;
  std::uint64_t available_bytes;
  Pressure      pressure;
};

/** @brief Converts page counts to bytes with overflow and range checks. */
[[nodiscard]] Result<MemorySample> derive_memory(const MemoryPages& pages) noexcept;
/** @brief Returns the stable string representation of a pressure level. */
[[nodiscard]] std::string_view pressure_name(Pressure pressure) noexcept;

/** @brief One-, five-, and fifteen-minute system load averages. */
struct LoadSample {
  double one_minute;
  double five_minutes;
  double fifteen_minutes;
};

/** @brief Filesystem capacity and bytes available to the current user. */
struct DiskSample {
  std::uint64_t total_bytes;
  std::uint64_t available_bytes;
};

/** @brief Converts filesystem block counts to bytes with overflow checks. */
[[nodiscard]] Result<DiskSample> derive_disk(std::uint64_t blocks, std::uint64_t available_blocks, std::uint64_t fragment_size) noexcept;

/** @brief Internal battery charge and external power state. */
struct BatterySample {
  std::uint8_t percent;
  bool         charging;
  bool         external_power;
};

/** @brief Monotonic received and sent byte counters for one interface. */
struct NetworkCounters {
  std::uint64_t received_bytes;
  std::uint64_t sent_bytes;
};

/** @brief Interface throughput measured in bytes per second. */
struct NetworkSample {
  double download_bytes_per_second;
  double upload_bytes_per_second;
};

/** @brief Derives interface rates; rejects a zero interval or counter rollback. */
[[nodiscard]] Result<NetworkSample>
derive_network(const NetworkCounters& before, const NetworkCounters& after, std::uint64_t elapsed_nanoseconds) noexcept;

} // namespace statwell

#endif // STATWELL_METRICS_HPP

//===---------------------------------------------------------------------------===//
