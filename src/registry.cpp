//===---------------------------------------------------------------------------===//
/**
 * @file registry.cpp
 * @author LCS.Dev - StatWell
 * @brief Built-in probe registration and per-metric value serializers.
 * @version 0.1
 * @date 2026-09-28
 *
 * @copyright MIT License 2026
 */
//===---------------------------------------------------------------------------===//

#include "registry.hpp"

#include "statwell/packages.hpp"
#include "statwell/probes.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace statwell {
namespace {

[[nodiscard]] std::int64_t unix_ms() noexcept {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void render_value(std::ostream& out, const CpuSample& value) {
  out << "\"user_percent\":" << value.user_percent << ",\"system_percent\":" << value.system_percent
      << ",\"total_percent\":" << value.total_percent;
}

void render_value(std::ostream& out, const MemorySample& value) {
  out << "\"total_bytes\":" << value.total_bytes << ",\"used_bytes\":" << value.used_bytes
      << ",\"available_bytes\":" << value.available_bytes << ",\"pressure\":\"" << pressure_name(value.pressure) << '"';
}

void render_value(std::ostream& out, const LoadSample& value) {
  out << "\"one_minute\":" << value.one_minute << ",\"five_minutes\":" << value.five_minutes
      << ",\"fifteen_minutes\":" << value.fifteen_minutes;
}

void render_value(std::ostream& out, const DiskSample& value) {
  out << "\"total_bytes\":" << value.total_bytes << ",\"available_bytes\":" << value.available_bytes;
}

void render_value(std::ostream& out, const BatterySample& value) {
  out << "\"percent\":" << static_cast<int>(value.percent) << ",\"charging\":" << (value.charging ? "true" : "false")
      << ",\"external_power\":" << (value.external_power ? "true" : "false");
}

void render_value(std::ostream& out, const NetworkSample& value) {
  out << "\"download_bytes_per_second\":" << value.download_bytes_per_second
      << ",\"upload_bytes_per_second\":" << value.upload_bytes_per_second;
}

void render_value(std::ostream& out, const UpdateSample& value) {
  out << "\"total\":" << value.total;
  if (value.has_breakdown)
    out << ",\"formulae\":" << value.formulae << ",\"casks\":" << value.casks;
}

template <typename T>
class State : public MetricSource {
public:
  void render(std::ostream& out, std::chrono::milliseconds cadence, std::int64_t duration_us) const final {
    out << "{\"sequence\":" << sequence_ << ",\"sampled_at_unix_ms\":" << attempted_at_ms_ << ",\"value_at_unix_ms\":" << value_at_ms_
        << ",\"max_age_ms\":" << cadence.count() * 3 << ",\"sample_duration_us\":" << duration_us << ",\"status\":\""
        << (sequence_ == 0 ? "unavailable"
            : last_ok_     ? "ok"
                           : "error")
        << '"';
    if (sequence_ > 0 && !last_ok_)
      out << ",\"error\":\"" << error_name(error_.code) << "\",\"native_code\":" << error_.native_code;
    if (has_value_) {
      out << ",\"value\":{";
      render_value(out, value_);
      out << '}';
    }
    out << '}';
  }

protected:
  void observe(Result<T> result) noexcept {
    ++sequence_;
    attempted_at_ms_ = unix_ms();
    if (result) {
      value_       = *result;
      value_at_ms_ = attempted_at_ms_;
      has_value_   = true;
      last_ok_     = true;
      error_       = {};
    } else {
      last_ok_ = false;
      error_   = result.error();
    }
  }

private:
  T             value_{};
  ProbeError    error_{};
  std::int64_t  attempted_at_ms_ = 0;
  std::int64_t  value_at_ms_     = 0;
  std::uint64_t sequence_        = 0;
  bool          has_value_       = false;
  bool          last_ok_         = false;
};

class CpuMetric final : public State<CpuSample> {
public:
  void sample() noexcept override { observe(probe_.sample()); }

private:
  CpuProbe probe_;
};

class MemoryMetric final : public State<MemorySample> {
public:
  void sample() noexcept override { observe(sample_memory()); }
};

class LoadMetric final : public State<LoadSample> {
public:
  void sample() noexcept override { observe(sample_load()); }
};

class DiskMetric final : public State<DiskSample> {
public:
  explicit DiskMetric(std::string path) : path_(std::move(path)) {}

  void sample() noexcept override { observe(sample_disk(path_)); }

private:
  std::string path_;
};

class BatteryMetric final : public State<BatterySample> {
public:
  void sample() noexcept override { observe(sample_battery()); }
};

class NetworkMetric final : public State<NetworkSample> {
public:
  explicit NetworkMetric(std::string interface_name) : probe_(std::move(interface_name)) {}

  void sample() noexcept override { observe(probe_.sample()); }

private:
  NetworkProbe probe_;
};

class PackageMetric final : public State<UpdateSample> {
public:
  PackageMetric(PackageKind kind, std::string executable, std::chrono::milliseconds timeout) :
      kind_(kind),
      executable_(std::move(executable)),
      timeout_(timeout) {}

  void sample() noexcept override {
    if (worker_.joinable())
      return;
    try {
      worker_ = std::jthread([this](const std::stop_token& stop) {
        const auto           started = std::chrono::steady_clock::now();
        Result<UpdateSample> value   = std::unexpected(ProbeError{ErrorCode::system_failure});
        try {
          value = sample_updates(kind_, executable_, timeout_, stop);
        } catch (...) {
          value = std::unexpected(ProbeError{ErrorCode::system_failure});
        }
        const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started).count();
        std::lock_guard lock(mutex_);
        completed_          = std::move(value);
        completed_duration_ = duration;
      });
    } catch (...) {
      observe(std::unexpected(ProbeError{ErrorCode::system_failure}));
    }
  }

  bool poll(std::int64_t& duration_us) noexcept override {
    std::optional<Result<UpdateSample>> value;
    {
      std::lock_guard lock(mutex_);
      if (!completed_)
        return false;
      value = std::move(completed_);
      completed_.reset();
      duration_us = completed_duration_;
    }
    worker_.join();
    observe(std::move(*value));
    return true;
  }

  [[nodiscard]] bool pending() const noexcept override { return worker_.joinable(); }

private:
  PackageKind                         kind_;
  std::string                         executable_;
  std::chrono::milliseconds           timeout_;
  mutable std::mutex                  mutex_;
  std::optional<Result<UpdateSample>> completed_;
  std::int64_t                        completed_duration_ = 0;
  std::jthread                        worker_;
};

} // namespace

std::vector<Registration> make_registry(const RuntimeOptions& options) {
  const auto                now = std::chrono::steady_clock::now();
  std::vector<Registration> entries;
  entries.reserve(6 + options.providers.size());
  entries.push_back({"cpu", std::chrono::seconds(2), std::make_unique<CpuMetric>(), now});
  entries.push_back({"memory", std::chrono::seconds(5), std::make_unique<MemoryMetric>(), now});
  entries.push_back({"load", std::chrono::seconds(5), std::make_unique<LoadMetric>(), now});
  entries.push_back({"disk", std::chrono::seconds(60), std::make_unique<DiskMetric>(options.disk_path), now});
  entries.push_back({"battery", std::chrono::seconds(30), std::make_unique<BatteryMetric>(), now});
  entries.push_back({"network", std::chrono::seconds(2), std::make_unique<NetworkMetric>(options.interface_name), now});
  for (const auto& provider : options.providers) {
    if (provider == "homebrew")
      entries.push_back(
          {"homebrew", std::chrono::hours(1),
           std::make_unique<PackageMetric>(PackageKind::homebrew, options.homebrew_bin, options.package_timeout), now});
    else if (provider == "pacman")
      entries.push_back(
          {"pacman", std::chrono::hours(1),
           std::make_unique<PackageMetric>(PackageKind::pacman, options.checkupdates_bin, options.package_timeout), now});
    else
      throw std::invalid_argument("unknown provider: " + provider);
  }
  for (const auto& [name, cadence] : options.cadence_overrides) {
    bool found = false;
    for (auto& entry : entries)
      if (entry.name == name) {
        entry.cadence = cadence;
        found         = true;
      }
    if (!found)
      throw std::invalid_argument("unknown probe cadence: " + name);
  }
  return entries;
}

} // namespace statwell

//===---------------------------------------------------------------------------===//
