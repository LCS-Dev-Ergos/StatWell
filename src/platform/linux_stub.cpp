#include "statwell/probes.hpp"

namespace statwell {
namespace {
constexpr ProbeError kUnsupported{ErrorCode::unsupported};
}

Result<CpuTicks> read_cpu_ticks() noexcept { return std::unexpected(kUnsupported); }
Result<MemorySample> sample_memory() noexcept { return std::unexpected(kUnsupported); }
Result<LoadSample> sample_load() noexcept { return std::unexpected(kUnsupported); }
Result<DiskSample> sample_disk(const std::string &) noexcept {
  return std::unexpected(kUnsupported);
}
Result<BatterySample> sample_battery() noexcept { return std::unexpected(kUnsupported); }
Result<NetworkCounters> read_network_counters(std::string_view) noexcept {
  return std::unexpected(kUnsupported);
}

} // namespace statwell
