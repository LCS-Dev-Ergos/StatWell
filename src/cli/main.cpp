//===---------------------------------------------------------------------------===//
/**
 * @file main.cpp
 * @author LCS.Dev - StatWell
 * @brief One-shot CLI with versioned JSON and key/value output.
 * @version 0.1
 * @date 2026-09-28
 *
 * @copyright MIT License 2026
 */
//===---------------------------------------------------------------------------===//

#include "statwell/packages.hpp"
#include "statwell/probes.hpp"
#include "statwell/runtime.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <locale>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

enum class Metric : std::uint8_t { cpu, memory, load, disk, battery, network, homebrew, pacman, count };
constexpr std::array<std::string_view, static_cast<std::size_t>(Metric::count)> kNames{"cpu",     "memory",  "load",     "disk",
                                                                                       "battery", "network", "homebrew", "pacman"};

struct Options {
  std::array<bool, kNames.size()> selected{true, true, true, true, true, false};

  bool        explicit_metrics = false;
  bool        json             = true;
  std::string disk_path;
  std::string interface_name;
  std::string homebrew_bin       = "/opt/homebrew/bin/brew";
  std::string checkupdates_bin   = "/usr/bin/checkupdates";
  int         package_timeout_ms = 10'000;
  int         interval_ms        = 200;
};

struct Field {
  std::string_view key;
  std::string      value;
  bool             quoted = false;
};

struct Entry {
  std::string_view     name;
  bool                 ok = false;
  statwell::ProbeError error{statwell::ErrorCode::unavailable};
  std::vector<Field>   fields;
};

void help() {
  std::cout << R"(NAME
  statwell - Sample Native System Status Metrics

SYNOPSIS
  statwell [sample] [SAMPLE OPTIONS]
  statwell snapshot [SERVICE OPTIONS]
  statwell daemon [SERVICE OPTIONS]
  statwell watch --metric NAME --event NAME [SERVICE OPTIONS]
  statwell [--help | --version]

COMMANDS
  sample     Read metrics once; this is the default command.
  snapshot   Read the daemon's shared snapshot, or sample once if absent.
  daemon     Keep an owner-only snapshot current for local readers.
  watch      Forward one metric to a SketchyBar event (macOS only).

METRICS
  cpu, memory, load, disk, battery, network, homebrew, pacman
  sample reads cpu, memory, load, disk, and battery by default. Network needs
  --interface; package checks run only when selected or enabled as providers.

SAMPLE OPTIONS
  --metric NAME           Select a metric; may be repeated.
  --format json|kv        Output format (default: json). Both use schema v1.
  --interval-ms N         CPU/network counter interval: 50..5000 (default: 200).

SERVICE OPTIONS
  --runtime-dir PATH      Private snapshot directory (default: user runtime dir).
  --provider NAME         Enable homebrew or pacman; may be repeated.
  --cadence NAME=MS       Daemon probe interval: 100..3600000 ms.
  --metric NAME           Metric to forward (watch only).
  --event NAME            SketchyBar event name (watch only).

SHARED OPTIONS
  --disk-path PATH        Filesystem to measure (default: home directory).
  --interface NAME        Interface for network rates; required for network.
  --homebrew-bin PATH     Absolute brew path (default: /opt/homebrew/bin/brew).
  --checkupdates-bin PATH Absolute checkupdates path (default: /usr/bin/checkupdates).
  --package-timeout-ms N  Package check deadline: 100..60000 ms (default: 10000).
  -h, --help              Show this help.
  --version               Show the program version (sample command).

OUTPUT AND EXIT STATUS
  Values use bytes, bytes/second, percentages, and load averages. Metric
  errors carry a stable code; unavailable readings never become zero.
  sample exits 0 if any selected metric succeeds, 1 if none succeeds, or 2
  for invalid arguments. snapshot exits 0 when it prints a snapshot; inspect
  each metric's status to distinguish successful and failed probes.

EXAMPLES
  statwell sample --metric cpu --metric memory --format json
  statwell sample --metric network --interface en0 --format kv
  statwell daemon --interface en0 --cadence battery=30000
  statwell daemon --provider homebrew --cadence homebrew=3600000
  statwell snapshot --runtime-dir /tmp/statwell-501
  statwell watch --metric network --event statwell_network --interface en0
)";
}

struct RuntimeArgs {
  statwell::RuntimeOptions options;
  std::string              metric;
  std::string              event;
};

bool parse_runtime(int argc, char** argv, RuntimeArgs& parsed) {
  const char* home         = std::getenv("HOME");
  parsed.options.disk_path = home != nullptr && *home != '\0' ? home : "/";
  for (int index = 2; index < argc; ++index) {
    const std::string_view arg(argv[index]);
    if (arg == "--help" || arg == "-h") {
      help();
      std::exit(0);
    }
    if (index + 1 >= argc)
      return false;
    const std::string_view value(argv[++index]);
    if (arg == "--runtime-dir")
      parsed.options.runtime_dir = value;
    else if (arg == "--disk-path")
      parsed.options.disk_path = value;
    else if (arg == "--interface")
      parsed.options.interface_name = value;
    else if (arg == "--homebrew-bin")
      parsed.options.homebrew_bin = value;
    else if (arg == "--checkupdates-bin")
      parsed.options.checkupdates_bin = value;
    else if (arg == "--provider") {
      if (value != "homebrew" && value != "pacman")
        return false;
      parsed.options.providers.emplace(value);
    } else if (arg == "--package-timeout-ms") {
      int milliseconds        = 0;
      const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), milliseconds);
      if (error != std::errc{} || end != value.data() + value.size() || milliseconds < 100 || milliseconds > 60'000)
        return false;
      parsed.options.package_timeout = std::chrono::milliseconds(milliseconds);
    } else if (arg == "--metric")
      parsed.metric = value;
    else if (arg == "--event")
      parsed.event = value;
    else if (arg == "--cadence") {
      const auto separator = value.find('=');
      if (separator == std::string_view::npos)
        return false;
      const auto name         = value.substr(0, separator);
      const auto duration     = value.substr(separator + 1);
      int        milliseconds = 0;
      const auto [end, error] = std::from_chars(duration.data(), duration.data() + duration.size(), milliseconds);
      if (error != std::errc{} || end != duration.data() + duration.size() || milliseconds < 100 || milliseconds > 3'600'000)
        return false;
      if (name.empty() || name.size() > 64 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-.") != std::string_view::npos)
        return false;
      parsed.options.cadence_overrides[std::string(name)] = std::chrono::milliseconds(milliseconds);
    } else
      return false;
  }
  return true;
}

bool parse(int argc, char** argv, Options& options) {
  int index = 1;
  if (index < argc && std::string_view(argv[index]) == "sample")
    ++index;
  for (; index < argc; ++index) {
    const std::string_view arg(argv[index]);
    if (arg == "--help" || arg == "-h") {
      help();
      std::exit(0);
    }
    if (arg == "--version") {
      std::cout << "statwell " << STATWELL_VERSION << '\n';
      std::exit(0);
    }
    if (index + 1 >= argc)
      return false;
    const std::string_view value(argv[++index]);
    if (arg == "--metric") {
      if (!options.explicit_metrics) {
        options.selected.fill(false);
        options.explicit_metrics = true;
      }
      bool found = false;
      for (std::size_t i = 0; i < kNames.size(); ++i) {
        if (value == kNames[i]) {
          options.selected[i] = true;
          found               = true;
        }
      }
      if (!found)
        return false;
    } else if (arg == "--format") {
      if (value != "json" && value != "kv")
        return false;
      options.json = value == "json";
    } else if (arg == "--disk-path") {
      options.disk_path = value;
    } else if (arg == "--interface") {
      options.interface_name = value;
    } else if (arg == "--homebrew-bin") {
      options.homebrew_bin = value;
    } else if (arg == "--checkupdates-bin") {
      options.checkupdates_bin = value;
    } else if (arg == "--package-timeout-ms") {
      int parsed              = 0;
      const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
      if (error != std::errc{} || end != value.data() + value.size() || parsed < 100 || parsed > 60'000)
        return false;
      options.package_timeout_ms = parsed;
    } else if (arg == "--interval-ms") {
      int parsed              = 0;
      const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
      if (error != std::errc{} || end != value.data() + value.size() || parsed < 50 || parsed > 5'000)
        return false;
      options.interval_ms = parsed;
    } else {
      return false;
    }
  }
  return !options.selected[static_cast<std::size_t>(Metric::network)] || !options.interface_name.empty();
}

std::string decimal(double value) {
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << std::fixed << std::setprecision(6) << value;
  return output.str();
}

template <typename T, typename Fill>
Entry entry(std::string_view name, const statwell::Result<T>& result, Fill fill) {
  Entry output{name, result.has_value(), {statwell::ErrorCode::unavailable}, {}};
  if (result) {
    fill(output.fields, *result);
  } else {
    output.error = result.error();
  }
  return output;
}

void print_json(const std::vector<Entry>& entries, std::int64_t timestamp) {
  std::cout << "{\"schema_version\":1,\"captured_at_unix_ms\":" << timestamp << ",\"metrics\":{";
  bool first_metric = true;
  for (const auto& metric : entries) {
    if (!std::exchange(first_metric, false))
      std::cout << ',';
    std::cout << '"' << metric.name << "\":{\"status\":\"";
    if (metric.ok) {
      std::cout << "ok\"";
      for (const auto& field : metric.fields) {
        std::cout << ",\"" << field.key << "\":";
        if (field.quoted)
          std::cout << '"';
        std::cout << field.value;
        if (field.quoted)
          std::cout << '"';
      }
    } else {
      std::cout << "error\",\"error\":\"" << statwell::error_name(metric.error.code) << "\",\"native_code\":" << metric.error.native_code;
    }
    std::cout << '}';
  }
  std::cout << "}}\n";
}

void print_kv(const std::vector<Entry>& entries, std::int64_t timestamp) {
  std::cout << "schema.version=1\ncaptured_at_unix_ms=" << timestamp << '\n';
  for (const auto& metric : entries) {
    std::cout << metric.name << ".status=" << (metric.ok ? "ok" : "error") << '\n';
    if (metric.ok) {
      for (const auto& field : metric.fields) {
        std::cout << metric.name << '.' << field.key << '=' << field.value << '\n';
      }
    } else {
      std::cout << metric.name << ".error=" << statwell::error_name(metric.error.code) << '\n';
      std::cout << metric.name << ".native_code=" << metric.error.native_code << '\n';
    }
  }
}

int run(int argc, char** argv) {
  Options     options;
  const char* home  = std::getenv("HOME");
  options.disk_path = home != nullptr && *home != '\0' ? home : "/";
  if (!parse(argc, argv, options)) {
    std::cerr << "statwell: invalid arguments (see --help)\n";
    return 2;
  }

  statwell::CpuProbe     cpu;
  statwell::NetworkProbe network(options.interface_name);

  std::optional<statwell::Result<statwell::CpuSample>>     first_cpu;
  std::optional<statwell::Result<statwell::NetworkSample>> first_network;
  if (options.selected[static_cast<std::size_t>(Metric::cpu)]) {
    first_cpu = cpu.sample();
  }
  if (options.selected[static_cast<std::size_t>(Metric::network)]) {
    first_network = network.sample();
  }
  if (options.selected[static_cast<std::size_t>(Metric::cpu)] || options.selected[static_cast<std::size_t>(Metric::network)]) {
    std::this_thread::sleep_for(std::chrono::milliseconds(options.interval_ms));
  }

  std::vector<Entry> entries;
  entries.reserve(kNames.size());
  if (options.selected[static_cast<std::size_t>(Metric::cpu)]) {
    auto result = cpu.sample();
    if (!result && first_cpu && !*first_cpu && first_cpu->error().code != statwell::ErrorCode::unavailable) {
      result = std::unexpected(first_cpu->error());
    }
    entries.push_back(entry("cpu", result, [](auto& fields, const auto& sample) {
      fields.push_back({"user_percent", decimal(sample.user_percent)});
      fields.push_back({"system_percent", decimal(sample.system_percent)});
      fields.push_back({"total_percent", decimal(sample.total_percent)});
    }));
  }
  if (options.selected[static_cast<std::size_t>(Metric::memory)]) {
    entries.push_back(entry("memory", statwell::sample_memory(), [](auto& fields, const auto& sample) {
      fields.push_back({"total_bytes", std::to_string(sample.total_bytes)});
      fields.push_back({"used_bytes", std::to_string(sample.used_bytes)});
      fields.push_back({"available_bytes", std::to_string(sample.available_bytes)});
      fields.push_back({"pressure", std::string(statwell::pressure_name(sample.pressure)), true});
    }));
  }
  if (options.selected[static_cast<std::size_t>(Metric::load)]) {
    entries.push_back(entry("load", statwell::sample_load(), [](auto& fields, const auto& sample) {
      fields.push_back({"one_minute", decimal(sample.one_minute)});
      fields.push_back({"five_minutes", decimal(sample.five_minutes)});
      fields.push_back({"fifteen_minutes", decimal(sample.fifteen_minutes)});
    }));
  }
  if (options.selected[static_cast<std::size_t>(Metric::disk)]) {
    entries.push_back(entry("disk", statwell::sample_disk(options.disk_path), [](auto& fields, const auto& sample) {
      fields.push_back({"total_bytes", std::to_string(sample.total_bytes)});
      fields.push_back({"available_bytes", std::to_string(sample.available_bytes)});
    }));
  }
  if (options.selected[static_cast<std::size_t>(Metric::battery)]) {
    entries.push_back(entry("battery", statwell::sample_battery(), [](auto& fields, const auto& sample) {
      fields.push_back({"percent", std::to_string(sample.percent)});
      fields.push_back({"charging", sample.charging ? "true" : "false"});
      fields.push_back({"external_power", sample.external_power ? "true" : "false"});
    }));
  }
  if (options.selected[static_cast<std::size_t>(Metric::network)]) {
    auto result = network.sample();
    if (!result && first_network && !*first_network && first_network->error().code != statwell::ErrorCode::unavailable) {
      result = std::unexpected(first_network->error());
    }
    entries.push_back(entry("network", result, [](auto& fields, const auto& sample) {
      fields.push_back({"download_bytes_per_second", decimal(sample.download_bytes_per_second)});
      fields.push_back({"upload_bytes_per_second", decimal(sample.upload_bytes_per_second)});
    }));
  }
  if (options.selected[static_cast<std::size_t>(Metric::homebrew)]) {
    entries.push_back(entry(
        "homebrew",
        statwell::sample_updates(
            statwell::PackageKind::homebrew, options.homebrew_bin, std::chrono::milliseconds(options.package_timeout_ms)),
        [](auto& fields, const auto& sample) {
          fields.push_back({"total", std::to_string(sample.total)});
          fields.push_back({"formulae", std::to_string(sample.formulae)});
          fields.push_back({"casks", std::to_string(sample.casks)});
        }));
  }
  if (options.selected[static_cast<std::size_t>(Metric::pacman)]) {
    entries.push_back(entry(
        "pacman",
        statwell::sample_updates(
            statwell::PackageKind::pacman, options.checkupdates_bin, std::chrono::milliseconds(options.package_timeout_ms)),
        [](auto& fields, const auto& sample) { fields.push_back({"total", std::to_string(sample.total)}); }));
  }

  const auto now       = std::chrono::system_clock::now();
  const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  if (options.json)
    print_json(entries, timestamp);
  else
    print_kv(entries, timestamp);
  return std::any_of(entries.begin(), entries.end(), [](const Entry& metric) { return metric.ok; }) ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
  try {
    if (argc > 1) {
      const std::string_view command(argv[1]);
      if (command == "daemon" || command == "snapshot" || command == "watch") {
        RuntimeArgs parsed;
        if (!parse_runtime(argc, argv, parsed)) {
          std::cerr << "statwell: invalid arguments (see --help)\n";
          return 2;
        }
        if (command == "daemon") {
          if (!parsed.metric.empty() || !parsed.event.empty())
            return 2;
          return statwell::run_daemon(parsed.options);
        }
        if (command == "snapshot") {
          if (!parsed.metric.empty() || !parsed.event.empty())
            return 2;
          const auto content = statwell::read_snapshot(parsed.options.runtime_dir);
          std::cout << (content ? *content : statwell::one_shot_snapshot(parsed.options));
          return 0;
        }
        if (parsed.metric.empty() || parsed.event.empty())
          return 2;
        return statwell::run_watch(parsed.options, parsed.metric, parsed.event);
      }
    }
    return run(argc, argv);
  } catch (const std::invalid_argument& error) {
    std::cerr << "statwell: " << error.what() << '\n';
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "statwell: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "statwell: unexpected failure\n";
    return 1;
  }
}

//===---------------------------------------------------------------------------===//
