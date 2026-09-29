//===---------------------------------------------------------------------------===//
/**
 * @file watch.cpp
 * @author LCS.Dev - StatWell
 * @brief Follow snapshot sequences and publish SketchyBar Mach events.
 * @version 0.1
 * @date 2026-09-28
 *
 * @copyright MIT License 2026
 */
//===---------------------------------------------------------------------------===//

#include "registry.hpp"
#include "statwell/runtime.hpp"
#include "watch_state.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef __APPLE__
#include <bootstrap.h>
#include <mach/mach.h>
#endif

namespace statwell {
namespace {

#ifdef __APPLE__
[[nodiscard]] bool safe_name(std::string_view name) {
  return !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](unsigned char character) {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9')
           || character == '_' || character == '-' || character == '.';
  });
}

class MachPort {
public:
  ~MachPort() { reset(); }

  MachPort(const MachPort&)            = delete;
  MachPort& operator=(const MachPort&) = delete;
  MachPort()                           = default;

  [[nodiscard]] bool send(const std::vector<std::string>& arguments) {
    if (port_ == MACH_PORT_NULL && !connect())
      return false;
    if (transmit(arguments))
      return true;
    reset();
    return connect() && transmit(arguments);
  }

private:
  [[nodiscard]] bool connect() {
    const char*            bar_name = std::getenv("BAR_NAME");
    const std::string_view name     = bar_name != nullptr ? bar_name : "sketchybar";
    if (!safe_name(name))
      return false;
    const std::string service = "git.felix." + std::string(name);
    return ::bootstrap_look_up(::bootstrap_port, service.c_str(), &port_) == KERN_SUCCESS;
  }

  void reset() {
    if (port_ != MACH_PORT_NULL)
      ::mach_port_deallocate(::mach_task_self(), port_);
    port_ = MACH_PORT_NULL;
  }

  [[nodiscard]] bool transmit(const std::vector<std::string>& arguments) const {
    std::vector<char> payload;
    for (const auto& argument : arguments) {
      payload.insert(payload.end(), argument.begin(), argument.end());
      payload.push_back('\0');
    }
    payload.push_back('\0');

    struct Message {
      mach_msg_header_t         header;
      mach_msg_body_t           body;
      mach_msg_ool_descriptor_t descriptor;
    } message{};

    message.header.msgh_remote_port    = port_;
    message.header.msgh_bits           = MACH_MSGH_BITS_SET(MACH_MSG_TYPE_COPY_SEND, 0, 0, MACH_MSGH_BITS_COMPLEX);
    message.header.msgh_size           = sizeof(message);
    message.body.msgh_descriptor_count = 1;
    message.descriptor.address         = payload.data();
    message.descriptor.size            = static_cast<mach_msg_size_t>(payload.size());
    message.descriptor.copy            = MACH_MSG_VIRTUAL_COPY;
    message.descriptor.deallocate      = false;
    message.descriptor.type            = MACH_MSG_OOL_DESCRIPTOR;
    return ::mach_msg(&message.header, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(message), 0, MACH_PORT_NULL, 100, MACH_PORT_NULL)
           == KERN_SUCCESS;
  }

  mach_port_t port_ = MACH_PORT_NULL;
};

struct FieldLookup {
  std::string_view object;
  std::string_view name;
};

[[nodiscard]] std::string_view field(const FieldLookup& lookup) {
  const auto [object, name] = lookup;
  const std::string key     = "\"" + std::string(name) + "\":";
  const auto        start   = object.find(key);
  if (start == std::string_view::npos)
    return {};
  const auto from = start + key.size();
  if (from >= object.size())
    return {};
  if (object[from] == '"') {
    const auto end = object.find('"', from + 1);
    return end == std::string_view::npos ? std::string_view{} : object.substr(from + 1, end - from - 1);
  }
  const auto end = object.find_first_of(",}", from);
  return object.substr(from, end == std::string_view::npos ? object.size() - from : end - from);
}

struct EventInput {
  std::string_view document;
  std::string_view metric;
  std::string_view event;
};

[[nodiscard]] std::vector<std::string> event_arguments(const EventInput& input) {
  const auto [document, metric, event] = input;
  const std::string key                = "\"" + std::string(metric) + "\":{";
  const auto        begin              = document.find(key);
  if (begin == std::string_view::npos)
    return {};
  const auto first_end   = document.find('}', begin);
  auto       value_begin = document.find("\"value\":{", begin);
  if (value_begin > first_end)
    value_begin = std::string_view::npos;
  const auto record_end = value_begin == std::string_view::npos ? first_end : document.find("}}", value_begin);
  if (record_end == std::string_view::npos)
    return {};
  const auto               record = document.substr(begin, record_end - begin + 1);
  std::vector<std::string> arguments{"--trigger", std::string(event)};
  for (const auto name : {"status", "sequence", "sampled_at_unix_ms", "value_at_unix_ms", "max_age_ms", "error", "native_code"}) {
    const auto value = field({record, name});
    if (!value.empty())
      arguments.emplace_back(std::string(name) + "=" + std::string(value));
  }
  if (value_begin != std::string_view::npos) {
    const auto value_end = document.find('}', value_begin);
    if (value_end == std::string_view::npos)
      return {};
    auto values = document.substr(value_begin + 9, value_end - value_begin - 9);
    while (!values.empty()) {
      const auto key_start = values.find('"');
      if (key_start == std::string_view::npos)
        break;
      const auto key_end = values.find('"', key_start + 1);
      if (key_end == std::string_view::npos)
        break;
      const auto name  = values.substr(key_start + 1, key_end - key_start - 1);
      const auto datum = field({values, name});
      if (!safe_name(name) || datum.empty() || !safe_name(datum))
        return {};
      arguments.emplace_back(std::string(name) + "=" + std::string(datum));
      const auto comma = values.find(',', key_end);
      if (comma == std::string_view::npos)
        break;
      values.remove_prefix(comma + 1);
    }
  }
  return arguments;
}

[[nodiscard]] std::int64_t argument_number(const std::vector<std::string>& arguments, std::string_view name) {
  for (const auto& argument : arguments) {
    if (!argument.starts_with(name) || argument.size() <= name.size() || argument[name.size()] != '=')
      continue;
    const auto   value      = std::string_view(argument).substr(name.size() + 1);
    std::int64_t parsed     = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (error == std::errc{} && end == value.data() + value.size())
      return parsed;
  }
  return 0;
}
#endif

} // namespace

namespace detail {

std::string event_identity(
    std::string_view instance, std::string_view sequence, std::int64_t value_at_ms, std::int64_t max_age_ms, std::int64_t now_ms) {
  // Lua's os.time() has one-second precision. Wait that extra second so the
  // consumer is certain to mark the last value stale when this event arrives.
  const bool expired = value_at_ms > 0 && max_age_ms > 0 && now_ms >= value_at_ms && now_ms - value_at_ms > max_age_ms
                       && now_ms - value_at_ms - max_age_ms >= 1'000;
  const bool future  = value_at_ms > 0 && max_age_ms > 0 && now_ms < value_at_ms;
  return std::string(instance) + ':' + std::string(sequence) + (future ? ":future" : expired ? ":expired" : ":current");
}

} // namespace detail

int run_watch(const RuntimeOptions& options, std::string_view metric, std::string_view event) {
#ifndef __APPLE__
  static_cast<void>(options);
  static_cast<void>(metric);
  static_cast<void>(event);
  std::cerr << "statwell: SketchyBar Mach watch is available on macOS only\n";
  return 2;
#else
  RuntimeOptions effective      = options;
  const bool     package_metric = metric == "homebrew" || metric == "pacman";
  if (package_metric)
    effective.providers.emplace(metric);
  const auto registrations = make_registry(effective);
  const auto selected =
      std::find_if(registrations.begin(), registrations.end(), [metric](const Registration& item) { return item.name == metric; });
  if (selected == registrations.end() || !safe_name(event)) {
    std::cerr << "statwell: invalid watch metric or event\n";
    return 2;
  }
  MachPort port;
  if (!port.send({"--add", "event", std::string(event)})) {
    std::cerr << "statwell: SketchyBar Mach service is unavailable\n";
    return 1;
  }
  std::string                previous;
  std::optional<std::string> fallback;
  auto                       next_fallback = std::chrono::steady_clock::time_point::min();
  for (;;) {
    auto cached = read_snapshot(effective.runtime_dir);
    if (cached)
      fallback.reset();
    else if (!fallback || std::chrono::steady_clock::now() >= next_fallback) {
      fallback          = one_shot_snapshot(effective);
      const auto fields = event_arguments({*fallback, metric, event});
      const bool valid  = std::find(fields.begin(), fields.end(), "status=ok") != fields.end();
      // A failed package check must not remain cached for the full hourly cadence.
      next_fallback = std::chrono::steady_clock::now()
                      + (package_metric ? (valid ? selected->cadence : std::chrono::seconds(30)) : std::chrono::seconds(2));
    }
    const auto& document  = cached ? *cached : *fallback;
    const auto  instance  = field({document, "instance_id"});
    const auto  arguments = event_arguments({document, metric, event});
    if (arguments.empty()) {
      std::cerr << "statwell: selected metric is absent from snapshot\n";
      return 1;
    }
    const auto sequence =
        std::find_if(arguments.begin(), arguments.end(), [](const std::string& argument) { return argument.starts_with("sequence="); });
    const auto value_at = argument_number(arguments, "value_at_unix_ms");
    const auto max_age  = argument_number(arguments, "max_age_ms");
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string identity = detail::event_identity(instance, sequence == arguments.end() ? "" : *sequence, value_at, max_age, now_ms);
    if (identity != previous) {
      if (!port.send(arguments)) {
        std::cerr << "statwell: unable to deliver SketchyBar event\n";
        return 1;
      }
      previous = identity;
    }
    std::this_thread::sleep_for(cached ? std::chrono::milliseconds(200) : std::chrono::seconds(2));
  }
#endif
}

} // namespace statwell

//===---------------------------------------------------------------------------===//
