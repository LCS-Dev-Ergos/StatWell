//===---------------------------------------------------------------------------===//
/**
 * @file darwin.cpp
 * @author StatWell contributors
 * @brief Native macOS probes built on public Mach, sysctl, and IOKit APIs.
 * @version 0.1
 * @date 2026-09-28
 *
 * @copyright MIT License 2026
 */
//===---------------------------------------------------------------------------===//

#include "statwell/probes.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/ps/IOPSKeys.h>
#include <IOKit/ps/IOPowerSources.h>
#include <ifaddrs.h>
#include <mach/mach.h>
#include <mach/processor_info.h>
#include <mach/vm_statistics.h>
#include <net/if.h>
#include <net/if_mib.h>
#include <net/if_var.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/sysctl.h>

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>

namespace statwell {
namespace {

class HostPort {
public:
  HostPort() noexcept : port_(mach_host_self()) {}

  HostPort(const HostPort&)            = delete;
  HostPort& operator=(const HostPort&) = delete;

  ~HostPort() {
    if (port_ != MACH_PORT_NULL)
      mach_port_deallocate(mach_task_self(), port_);
  }

  [[nodiscard]] host_t get() const noexcept { return port_; }

private:
  host_t port_;
};

class ProcessorInfo {
public:
  ProcessorInfo()                                = default;
  ProcessorInfo(const ProcessorInfo&)            = delete;
  ProcessorInfo& operator=(const ProcessorInfo&) = delete;

  ~ProcessorInfo() {
    if (data_ != nullptr) {
      vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(data_), static_cast<vm_size_t>(count_) * sizeof(integer_t));
    }
  }

  [[nodiscard]] processor_info_array_t* data_address() noexcept { return &data_; }

  [[nodiscard]] mach_msg_type_number_t* count_address() noexcept { return &count_; }

  [[nodiscard]] processor_info_array_t data() const noexcept { return data_; }

  [[nodiscard]] mach_msg_type_number_t count() const noexcept { return count_; }

private:
  processor_info_array_t data_  = nullptr;
  mach_msg_type_number_t count_ = 0;
};

class CfOwned {
public:
  explicit CfOwned(CFTypeRef value) noexcept : value_(value) {}

  CfOwned(const CfOwned&)            = delete;
  CfOwned& operator=(const CfOwned&) = delete;

  ~CfOwned() {
    if (value_ != nullptr)
      CFRelease(value_);
  }

  [[nodiscard]] CFTypeRef get() const noexcept { return value_; }

private:
  CFTypeRef value_;
};

[[nodiscard]] Result<NetworkCounters> interface_counters(std::string_view name) noexcept {
  // Interface indices start at one and can have gaps; IFCOUNT is not an index.
  char interface_name[IFNAMSIZ]{};
  std::memcpy(interface_name, name.data(), name.size());
  const auto index = if_nametoindex(interface_name);
  if (index == 0)
    return std::unexpected(ProbeError{ErrorCode::unavailable});
  if (index > static_cast<unsigned int>(std::numeric_limits<int>::max()))
    return std::unexpected(ProbeError{ErrorCode::invalid_input});
  int       mib[] = {CTL_NET, PF_LINK, NETLINK_GENERIC, IFMIB_IFDATA, static_cast<int>(index), IFDATA_GENERAL};
  ifmibdata data{};
  size_t    size = sizeof(data);
  if (sysctl(mib, 6, &data, &size, nullptr, 0) != 0) {
    return std::unexpected(ProbeError{ErrorCode::system_failure, errno});
  }
  const auto length = strnlen(data.ifmd_name, sizeof(data.ifmd_name));
  if (size != sizeof(data) || std::string_view(data.ifmd_name, length) != name)
    return std::unexpected(ProbeError{ErrorCode::unavailable});
  return NetworkCounters{data.ifmd_data.ifi_ibytes, data.ifmd_data.ifi_obytes};
}

} // namespace

Result<CpuTicks> read_cpu_ticks() noexcept {
  HostPort host;
  if (host.get() == MACH_PORT_NULL) {
    return std::unexpected(ProbeError{ErrorCode::system_failure});
  }
  natural_t     processors = 0;
  ProcessorInfo info;
  const auto    status = host_processor_info(host.get(), PROCESSOR_CPU_LOAD_INFO, &processors, info.data_address(), info.count_address());
  if (status != KERN_SUCCESS) {
    return std::unexpected(ProbeError{ErrorCode::system_failure, status});
  }
  if (processors == 0 || info.data() == nullptr || info.count() / CPU_STATE_MAX < processors) {
    return std::unexpected(ProbeError{ErrorCode::invalid_input});
  }
  CpuTicks total{};
  for (natural_t i = 0; i < processors; ++i) {
    const auto base = static_cast<std::size_t>(i) * CPU_STATE_MAX;
    total.user += static_cast<std::uint32_t>(info.data()[base + CPU_STATE_USER]);
    total.system += static_cast<std::uint32_t>(info.data()[base + CPU_STATE_SYSTEM]);
    total.idle += static_cast<std::uint32_t>(info.data()[base + CPU_STATE_IDLE]);
    total.nice += static_cast<std::uint32_t>(info.data()[base + CPU_STATE_NICE]);
  }
  return total;
}

Result<MemorySample> sample_memory() noexcept {
  HostPort host;
  if (host.get() == MACH_PORT_NULL) {
    return std::unexpected(ProbeError{ErrorCode::system_failure});
  }
  vm_statistics64_data_t stats{};
  mach_msg_type_number_t count  = HOST_VM_INFO64_COUNT;
  const auto             status = host_statistics64(host.get(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&stats), &count);
  if (status != KERN_SUCCESS || count < HOST_VM_INFO64_COUNT) {
    return std::unexpected(ProbeError{ErrorCode::system_failure, status});
  }
  vm_size_t  page_size   = 0;
  const auto page_status = host_page_size(host.get(), &page_size);
  if (page_status != KERN_SUCCESS) {
    return std::unexpected(ProbeError{ErrorCode::system_failure, page_status});
  }
  std::uint64_t total_bytes = 0;
  size_t        total_size  = sizeof(total_bytes);
  if (sysctlbyname("hw.memsize", &total_bytes, &total_size, nullptr, 0) != 0 || total_size != sizeof(total_bytes)) {
    return std::unexpected(ProbeError{ErrorCode::system_failure, errno});
  }
  int      raw_pressure  = 0;
  size_t   pressure_size = sizeof(raw_pressure);
  Pressure pressure      = Pressure::unknown;
  if (sysctlbyname("kern.memorystatus_vm_pressure_level", &raw_pressure, &pressure_size, nullptr, 0) == 0
      && pressure_size == sizeof(raw_pressure)) {
    if (raw_pressure == 1)
      pressure = Pressure::normal;
    if (raw_pressure == 2)
      pressure = Pressure::warning;
    if (raw_pressure == 4)
      pressure = Pressure::critical;
  }
  return derive_memory(
      MemoryPages{
          total_bytes, page_size, stats.internal_page_count, stats.purgeable_count, stats.wire_count, stats.compressor_page_count,
          pressure});
}

Result<LoadSample> sample_load() noexcept {
  double values[3]{};
  if (getloadavg(values, 3) != 3) {
    return std::unexpected(ProbeError{ErrorCode::system_failure, errno});
  }
  return LoadSample{values[0], values[1], values[2]};
}

Result<DiskSample> sample_disk(const std::string& path) noexcept {
  if (path.empty())
    return std::unexpected(ProbeError{ErrorCode::invalid_input});
  struct statvfs data{};
  if (statvfs(path.c_str(), &data) != 0) {
    return std::unexpected(ProbeError{ErrorCode::system_failure, errno});
  }
  return derive_disk(data.f_blocks, data.f_bavail, data.f_frsize);
}

Result<BatterySample> sample_battery() noexcept {
  CfOwned info(IOPSCopyPowerSourcesInfo());
  if (info.get() == nullptr) {
    return std::unexpected(ProbeError{ErrorCode::system_failure});
  }
  CfOwned sources(IOPSCopyPowerSourcesList(info.get()));
  if (sources.get() == nullptr || CFGetTypeID(sources.get()) != CFArrayGetTypeID()) {
    return std::unexpected(ProbeError{ErrorCode::system_failure});
  }
  const auto list = static_cast<CFArrayRef>(sources.get());
  for (CFIndex i = 0; i < CFArrayGetCount(list); ++i) {
    const auto description = IOPSGetPowerSourceDescription(info.get(), CFArrayGetValueAtIndex(list, i));
    if (description == nullptr)
      continue;
    const auto transport = CFDictionaryGetValue(description, CFSTR(kIOPSTransportTypeKey));
    if (transport == nullptr || CFGetTypeID(transport) != CFStringGetTypeID() || !CFEqual(transport, CFSTR(kIOPSInternalType)))
      continue;
    const auto current = CFDictionaryGetValue(description, CFSTR(kIOPSCurrentCapacityKey));
    const auto maximum = CFDictionaryGetValue(description, CFSTR(kIOPSMaxCapacityKey));
    if (current == nullptr || maximum == nullptr || CFGetTypeID(current) != CFNumberGetTypeID()
        || CFGetTypeID(maximum) != CFNumberGetTypeID()) {
      return std::unexpected(ProbeError{ErrorCode::invalid_input});
    }
    int current_value = 0;
    int maximum_value = 0;
    if (!CFNumberGetValue(static_cast<CFNumberRef>(current), kCFNumberIntType, &current_value)
        || !CFNumberGetValue(static_cast<CFNumberRef>(maximum), kCFNumberIntType, &maximum_value) || maximum_value <= 0 || current_value < 0
        || current_value > maximum_value) {
      return std::unexpected(ProbeError{ErrorCode::invalid_input});
    }
    const auto charging_value = CFDictionaryGetValue(description, CFSTR(kIOPSIsChargingKey));
    const bool charging       = charging_value != nullptr && CFGetTypeID(charging_value) == CFBooleanGetTypeID()
                                && CFBooleanGetValue(static_cast<CFBooleanRef>(charging_value));
    const auto source_value   = CFDictionaryGetValue(description, CFSTR(kIOPSPowerSourceStateKey));
    const bool external =
        source_value != nullptr && CFGetTypeID(source_value) == CFStringGetTypeID() && CFEqual(source_value, CFSTR(kIOPSACPowerValue));
    const auto percent = static_cast<std::uint8_t>((100LL * current_value + maximum_value / 2) / maximum_value);
    return BatterySample{percent, charging, external};
  }
  return std::unexpected(ProbeError{ErrorCode::unsupported});
}

Result<NetworkCounters> read_network_counters(std::string_view interface_name) noexcept {
  if (interface_name.empty() || interface_name.size() >= IFNAMSIZ) {
    return std::unexpected(ProbeError{ErrorCode::invalid_input});
  }
  ifaddrs* raw = nullptr;
  if (getifaddrs(&raw) != 0) {
    return std::unexpected(ProbeError{ErrorCode::system_failure, errno});
  }
  const std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> interfaces(raw, freeifaddrs);
  bool                                                   found = false;
  for (auto* current = interfaces.get(); current != nullptr; current = current->ifa_next) {
    if (current->ifa_name != nullptr && current->ifa_addr != nullptr && current->ifa_addr->sa_family == AF_LINK
        && interface_name == current->ifa_name && (current->ifa_flags & IFF_UP) != 0) {
      found = true;
      break;
    }
  }
  if (!found)
    return std::unexpected(ProbeError{ErrorCode::unavailable});
  return interface_counters(interface_name);
}

} // namespace statwell

//===---------------------------------------------------------------------------===//
