#ifndef STATWELL_PROBES_HPP
#define STATWELL_PROBES_HPP

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include "statwell/metrics.hpp"

namespace statwell {

[[nodiscard]] Result<CpuTicks> read_cpu_ticks() noexcept;
[[nodiscard]] Result<MemorySample> sample_memory() noexcept;
[[nodiscard]] Result<LoadSample> sample_load() noexcept;
[[nodiscard]] Result<DiskSample> sample_disk(const std::string &path) noexcept;
[[nodiscard]] Result<BatterySample> sample_battery() noexcept;
[[nodiscard]] Result<NetworkCounters>
read_network_counters(std::string_view interface_name) noexcept;

class CpuProbe {
public:
  [[nodiscard]] Result<CpuSample> sample() noexcept;

private:
  std::optional<CpuTicks> previous_;
};

class NetworkProbe {
public:
  explicit NetworkProbe(std::string interface_name);
  [[nodiscard]] Result<NetworkSample> sample() noexcept;

private:
  std::string interface_name_;
  std::optional<NetworkCounters> previous_;
  std::chrono::steady_clock::time_point previous_time_{};
};

} // namespace statwell

#endif // STATWELL_PROBES_HPP
