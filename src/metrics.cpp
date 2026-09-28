//===---------------------------------------------------------------------------===//
/**
 * @file metrics.cpp
 * @author StatWell contributors
 * @brief Platform-independent metric calculations and rate samplers.
 * @version 0.1
 * @date 2026-09-28
 *
 * @copyright MIT License 2026
 */
//===---------------------------------------------------------------------------===//

#include "statwell/metrics.hpp"

#include "statwell/probes.hpp"

#include <limits>
#include <utility>

namespace statwell {

std::string_view error_name(ErrorCode code) noexcept {
  switch (code) {
  case ErrorCode::unavailable:
    return "unavailable";
  case ErrorCode::unsupported:
    return "unsupported";
  case ErrorCode::invalid_input:
    return "invalid_input";
  case ErrorCode::system_failure:
    return "system_failure";
  }
  return "unknown";
}

Result<CpuSample> derive_cpu(const CpuTicks& before, const CpuTicks& after) noexcept {
  if (after.user < before.user || after.system < before.system || after.idle < before.idle || after.nice < before.nice) {
    return std::unexpected(ProbeError{ErrorCode::unavailable});
  }
  const double user   = static_cast<double>(after.user - before.user) + static_cast<double>(after.nice - before.nice);
  const double system = static_cast<double>(after.system - before.system);
  const double idle   = static_cast<double>(after.idle - before.idle);
  const double total  = user + system + idle;
  if (total == 0.0) {
    return std::unexpected(ProbeError{ErrorCode::unavailable});
  }
  return CpuSample{100.0 * user / total, 100.0 * system / total, 100.0 * (user + system) / total};
}

Result<MemorySample> derive_memory(const MemoryPages& pages) noexcept {
  if (pages.page_size == 0 || pages.total_bytes == 0 || pages.purgeable > pages.internal) {
    return std::unexpected(ProbeError{ErrorCode::invalid_input});
  }
  const auto     anonymous = pages.internal - pages.purgeable;
  constexpr auto max       = std::numeric_limits<std::uint64_t>::max();
  if (anonymous > max - pages.wired || anonymous + pages.wired > max - pages.compressor) {
    return std::unexpected(ProbeError{ErrorCode::invalid_input});
  }
  const auto used_pages = anonymous + pages.wired + pages.compressor;
  if (used_pages > pages.total_bytes / pages.page_size) {
    return std::unexpected(ProbeError{ErrorCode::invalid_input});
  }
  const auto used = used_pages * pages.page_size;
  return MemorySample{pages.total_bytes, used, pages.total_bytes - used, pages.pressure};
}

std::string_view pressure_name(Pressure pressure) noexcept {
  switch (pressure) {
  case Pressure::unknown:
    return "unknown";
  case Pressure::normal:
    return "normal";
  case Pressure::warning:
    return "warning";
  case Pressure::critical:
    return "critical";
  }
  return "unknown";
}

Result<DiskSample> derive_disk(std::uint64_t blocks, std::uint64_t available_blocks, std::uint64_t fragment_size) noexcept {
  constexpr auto max = std::numeric_limits<std::uint64_t>::max();
  if (fragment_size == 0 || blocks == 0 || available_blocks > blocks || blocks > max / fragment_size) {
    return std::unexpected(ProbeError{ErrorCode::invalid_input});
  }
  return DiskSample{blocks * fragment_size, available_blocks * fragment_size};
}

Result<NetworkSample>
derive_network(const NetworkCounters& before, const NetworkCounters& after, std::uint64_t elapsed_nanoseconds) noexcept {
  if (elapsed_nanoseconds == 0 || after.received_bytes < before.received_bytes || after.sent_bytes < before.sent_bytes) {
    return std::unexpected(ProbeError{ErrorCode::unavailable});
  }
  const double seconds = static_cast<double>(elapsed_nanoseconds) / 1'000'000'000.0;
  return NetworkSample{
      static_cast<double>(after.received_bytes - before.received_bytes) / seconds,
      static_cast<double>(after.sent_bytes - before.sent_bytes) / seconds};
}

Result<CpuSample> CpuProbe::sample() noexcept {
  auto current = read_cpu_ticks();
  if (!current)
    return std::unexpected(current.error());
  if (!previous_) {
    previous_ = *current;
    return std::unexpected(ProbeError{ErrorCode::unavailable});
  }
  const auto sample = derive_cpu(*previous_, *current);
  previous_         = *current;
  return sample;
}

NetworkProbe::NetworkProbe(std::string interface_name) : interface_name_(std::move(interface_name)) {
}

Result<NetworkSample> NetworkProbe::sample() noexcept {
  auto current = read_network_counters(interface_name_);
  if (!current)
    return std::unexpected(current.error());
  const auto now = std::chrono::steady_clock::now();
  if (!previous_) {
    previous_      = *current;
    previous_time_ = now;
    return std::unexpected(ProbeError{ErrorCode::unavailable});
  }
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(now - previous_time_).count();
  const auto sample  = derive_network(*previous_, *current, elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0);
  previous_          = *current;
  previous_time_     = now;
  return sample;
}

} // namespace statwell

//===---------------------------------------------------------------------------===//
