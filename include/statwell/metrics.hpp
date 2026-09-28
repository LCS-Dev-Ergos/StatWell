#ifndef STATWELL_METRICS_HPP
#define STATWELL_METRICS_HPP

#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>

namespace statwell {

enum class ErrorCode : std::uint8_t {
  unavailable,
  unsupported,
  invalid_input,
  system_failure,
};

struct ProbeError {
  ErrorCode code = ErrorCode::unavailable;
  int native_code = 0;
};

template <typename T> using Result = std::expected<T, ProbeError>;

[[nodiscard]] std::string_view error_name(ErrorCode code) noexcept;

struct CpuTicks {
  std::uint64_t user;
  std::uint64_t system;
  std::uint64_t idle;
  std::uint64_t nice;
};

struct CpuSample {
  double user_percent;
  double system_percent;
  double total_percent;
};

[[nodiscard]] Result<CpuSample> derive_cpu(const CpuTicks &before, const CpuTicks &after) noexcept;

enum class Pressure : std::uint8_t { unknown, normal, warning, critical };

struct MemoryPages {
  std::uint64_t total_bytes;
  std::uint64_t page_size;
  std::uint64_t internal;
  std::uint64_t purgeable;
  std::uint64_t wired;
  std::uint64_t compressor;
  Pressure pressure;
};

struct MemorySample {
  std::uint64_t total_bytes;
  std::uint64_t used_bytes;
  std::uint64_t available_bytes;
  Pressure pressure;
};

[[nodiscard]] Result<MemorySample> derive_memory(const MemoryPages &pages) noexcept;
[[nodiscard]] std::string_view pressure_name(Pressure pressure) noexcept;

struct LoadSample {
  double one_minute;
  double five_minutes;
  double fifteen_minutes;
};

struct DiskSample {
  std::uint64_t total_bytes;
  std::uint64_t available_bytes;
};

[[nodiscard]] Result<DiskSample> derive_disk(std::uint64_t blocks, std::uint64_t available_blocks,
                                             std::uint64_t fragment_size) noexcept;

struct BatterySample {
  std::uint8_t percent;
  bool charging;
  bool external_power;
};

struct NetworkCounters {
  std::uint64_t received_bytes;
  std::uint64_t sent_bytes;
};

struct NetworkSample {
  double download_bytes_per_second;
  double upload_bytes_per_second;
};

[[nodiscard]] Result<NetworkSample> derive_network(const NetworkCounters &before,
                                                   const NetworkCounters &after,
                                                   std::uint64_t elapsed_nanoseconds) noexcept;

} // namespace statwell

#endif // STATWELL_METRICS_HPP
