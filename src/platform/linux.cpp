//===---------------------------------------------------------------------------===//
/**
 * @file linux.cpp
 * @author LCS.Dev - StatWell
 * @brief Native Linux probes using bounded procfs and sysfs reads.
 * @version 0.1
 * @date 2026-09-28
 *
 * @copyright MIT License 2026
 */
//===---------------------------------------------------------------------------===//

#include "linux_parsers.hpp"
#include "statwell/probes.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <net/if.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <string_view>

namespace statwell {
namespace {

constexpr std::size_t kMaximumFileBytes = std::size_t{64} * 1'024;
using FileBuffer                        = std::array<char, kMaximumFileBytes>;

class FileDescriptor {
public:
  explicit FileDescriptor(int value) noexcept : value_(value) {}

  FileDescriptor(const FileDescriptor&)            = delete;
  FileDescriptor& operator=(const FileDescriptor&) = delete;

  ~FileDescriptor() {
    if (value_ >= 0)
      close(value_);
  }

  [[nodiscard]] int get() const noexcept { return value_; }

private:
  int value_;
};

[[nodiscard]] Result<std::string_view> read_file(const char* path, std::span<char> buffer) noexcept {
  FileDescriptor file(open(path, O_RDONLY | O_CLOEXEC));
  if (file.get() < 0)
    return std::unexpected(ProbeError{ErrorCode::system_failure, errno});
  std::size_t used = 0;
  while (used < buffer.size()) {
    const auto count = read(file.get(), buffer.data() + used, buffer.size() - used);
    if (count < 0) {
      if (errno == EINTR)
        continue;
      return std::unexpected(ProbeError{ErrorCode::system_failure, errno});
    }
    if (count == 0)
      return std::string_view(buffer.data(), used);
    used += static_cast<std::size_t>(count);
  }
  char    excess = 0;
  ssize_t count;
  do {
    count = read(file.get(), &excess, 1);
  } while (count < 0 && errno == EINTR);
  if (count < 0)
    return std::unexpected(ProbeError{ErrorCode::system_failure, errno});
  if (count > 0)
    return std::unexpected(ProbeError{ErrorCode::invalid_input});
  return std::string_view(buffer.data(), used);
}

[[nodiscard]] bool make_supply_path(char* output, std::size_t capacity, std::string_view name, const char* attribute) noexcept {
  const int length =
      std::snprintf(output, capacity, "/sys/class/power_supply/%.*s/%s", static_cast<int>(name.size()), name.data(), attribute);
  return length > 0 && static_cast<std::size_t>(length) < capacity;
}

} // namespace

//===----- SYSTEM PROBES -------------------------------------------------------===//

Result<CpuTicks> read_cpu_ticks() noexcept {
  FileBuffer buffer{};
  const auto text = read_file("/proc/stat", buffer);
  if (!text)
    return std::unexpected(text.error());
  return detail::parse_proc_stat(*text);
}

Result<MemorySample> sample_memory() noexcept {
  FileBuffer buffer{};
  const auto text = read_file("/proc/meminfo", buffer);
  if (!text)
    return std::unexpected(text.error());
  FileBuffer pressure_buffer{};
  const auto pressure = read_file("/proc/pressure/memory", pressure_buffer);
  return detail::parse_meminfo({*text, pressure ? *pressure : std::string_view{}});
}

Result<LoadSample> sample_load() noexcept {
  double values[3]{};
  if (getloadavg(values, 3) != 3)
    return std::unexpected(ProbeError{ErrorCode::system_failure, errno});
  return LoadSample{values[0], values[1], values[2]};
}

Result<DiskSample> sample_disk(const std::string& path) noexcept {
  if (path.empty())
    return std::unexpected(ProbeError{ErrorCode::invalid_input});
  struct statvfs data{};
  if (statvfs(path.c_str(), &data) != 0)
    return std::unexpected(ProbeError{ErrorCode::system_failure, errno});
  return derive_disk(data.f_blocks, data.f_bavail, data.f_frsize);
}

Result<BatterySample> sample_battery() noexcept {
  DIR* raw = opendir("/sys/class/power_supply");
  if (raw == nullptr) {
    const auto code = errno == ENOENT ? ErrorCode::unsupported : ErrorCode::system_failure;
    return std::unexpected(ProbeError{code, errno});
  }
  const std::unique_ptr<DIR, decltype(&closedir)> directory(raw, closedir);

  char       battery_name[256]{};
  bool       external_power = false;
  FileBuffer buffer{};
  char       path[512]{};
  while (true) {
    errno             = 0;
    const auto* entry = readdir(directory.get());
    if (entry == nullptr) {
      if (errno != 0)
        return std::unexpected(ProbeError{ErrorCode::system_failure, errno});
      break;
    }
    const std::string_view name(entry->d_name);
    if (name == "." || name == ".." || !make_supply_path(path, sizeof(path), name, "type"))
      continue;
    const auto type = read_file(path, buffer);
    if (!type)
      continue;
    if (*type == "Battery\n" || *type == "Battery") {
      if (battery_name[0] == '\0' || name < std::string_view(battery_name)) {
        if (name.size() >= sizeof(battery_name))
          return std::unexpected(ProbeError{ErrorCode::invalid_input});
        std::memcpy(battery_name, name.data(), name.size());
        battery_name[name.size()] = '\0';
      }
    } else if (make_supply_path(path, sizeof(path), name, "online")) {
      const auto online = read_file(path, buffer);
      if (online && (*online == "1\n" || *online == "1"))
        external_power = true;
    }
  }
  if (battery_name[0] == '\0')
    return std::unexpected(ProbeError{ErrorCode::unsupported});

  const std::string_view name(battery_name);
  if (!make_supply_path(path, sizeof(path), name, "capacity"))
    return std::unexpected(ProbeError{ErrorCode::invalid_input});
  const auto capacity = read_file(path, buffer);
  if (!capacity)
    return std::unexpected(capacity.error());
  FileBuffer status_buffer{};
  if (!make_supply_path(path, sizeof(path), name, "status"))
    return std::unexpected(ProbeError{ErrorCode::invalid_input});
  const auto status = read_file(path, status_buffer);
  if (!status)
    return std::unexpected(status.error());
  return detail::parse_battery({*capacity, *status, external_power});
}

Result<NetworkCounters> read_network_counters(std::string_view interface_name) noexcept {
  if (interface_name.empty() || interface_name.size() >= IFNAMSIZ)
    return std::unexpected(ProbeError{ErrorCode::invalid_input});
  FileBuffer buffer{};
  const auto text = read_file("/proc/net/dev", buffer);
  if (!text)
    return std::unexpected(text.error());
  return detail::parse_net_dev(*text, interface_name);
}

} // namespace statwell

//===---------------------------------------------------------------------------===//
