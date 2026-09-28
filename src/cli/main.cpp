#include "statwell/probes.hpp"

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

enum class Metric : std::uint8_t { cpu, memory, load, disk, battery, network, count };
constexpr std::array<std::string_view, static_cast<std::size_t>(Metric::count)> kNames{
    "cpu", "memory", "load", "disk", "battery", "network"};

struct Options {
  std::array<bool, kNames.size()> selected{true, true, true, true, true, false};
  bool explicit_metrics = false;
  bool json = true;
  std::string disk_path;
  std::string interface_name;
  int interval_ms = 200;
};

struct Field {
  std::string_view key;
  std::string value;
  bool quoted = false;
};

struct Entry {
  std::string_view name;
  bool ok = false;
  statwell::ProbeError error{statwell::ErrorCode::unavailable};
  std::vector<Field> fields;
};

void help() {
  std::cout << R"(NAME
  statwell - sample native system status metrics

SYNOPSIS
  statwell sample [OPTIONS]
  statwell --help | --version

DESCRIPTION
  Read selected metrics once using native operating-system APIs. CPU and
  network rates use two counter reads separated by --interval-ms. Values use
  bytes, bytes per second, percentages, and load averages as named.

OPTIONS
  --metric NAME       Select a metric; repeat for cpu, memory, load, disk,
                      battery, or network. Default: all except network.
  --format FORMAT     json (default) or kv. Both formats have version 1.
  --disk-path PATH    Filesystem path to measure. Default: home directory.
  --interface NAME    Network interface to measure; required for network.
  --interval-ms N     Counter interval, 50..5000 ms. Default: 200 ms.
  -h, --help          Show this help.
  --version           Show the program version.

OUTPUT
  Each selected metric has status ok or error. Errors include a stable code;
  unsupported and unavailable readings are never reported as zero. The JSON
  schema and key names are versioned separately from the program version.

EXAMPLES
  statwell sample --metric cpu --metric memory --format json
  statwell sample --metric network --interface en0 --format kv

EXIT STATUS
  0  At least one selected metric was sampled successfully.
  1  No selected metric was sampled successfully.
  2  Invalid command-line arguments.
)";
}

bool parse(int argc, char **argv, Options &options) {
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
          found = true;
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
    } else if (arg == "--interval-ms") {
      int parsed = 0;
      const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
      if (error != std::errc{} || end != value.data() + value.size() || parsed < 50 ||
          parsed > 5000)
        return false;
      options.interval_ms = parsed;
    } else {
      return false;
    }
  }
  return !options.selected[static_cast<std::size_t>(Metric::network)] ||
         !options.interface_name.empty();
}

std::string decimal(double value) {
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << std::fixed << std::setprecision(6) << value;
  return output.str();
}

template <typename T, typename Fill>
Entry entry(std::string_view name, const statwell::Result<T> &result, Fill fill) {
  Entry output{name, result.has_value(), {statwell::ErrorCode::unavailable}, {}};
  if (result) {
    fill(output.fields, *result);
  } else {
    output.error = result.error();
  }
  return output;
}

void print_json(const std::vector<Entry> &entries, std::int64_t timestamp) {
  std::cout << "{\"schema_version\":1,\"captured_at_unix_ms\":" << timestamp << ",\"metrics\":{";
  bool first_metric = true;
  for (const auto &metric : entries) {
    if (!std::exchange(first_metric, false))
      std::cout << ',';
    std::cout << '"' << metric.name << "\":{\"status\":\"";
    if (metric.ok) {
      std::cout << "ok\"";
      for (const auto &field : metric.fields) {
        std::cout << ",\"" << field.key << "\":";
        if (field.quoted)
          std::cout << '"';
        std::cout << field.value;
        if (field.quoted)
          std::cout << '"';
      }
    } else {
      std::cout << "error\",\"error\":\"" << statwell::error_name(metric.error.code)
                << "\",\"native_code\":" << metric.error.native_code;
    }
    std::cout << '}';
  }
  std::cout << "}}\n";
}

void print_kv(const std::vector<Entry> &entries, std::int64_t timestamp) {
  std::cout << "schema.version=1\ncaptured_at_unix_ms=" << timestamp << '\n';
  for (const auto &metric : entries) {
    std::cout << metric.name << ".status=" << (metric.ok ? "ok" : "error") << '\n';
    if (metric.ok) {
      for (const auto &field : metric.fields) {
        std::cout << metric.name << '.' << field.key << '=' << field.value << '\n';
      }
    } else {
      std::cout << metric.name << ".error=" << statwell::error_name(metric.error.code) << '\n';
      std::cout << metric.name << ".native_code=" << metric.error.native_code << '\n';
    }
  }
}

int run(int argc, char **argv) {
  Options options;
  const char *home = std::getenv("HOME");
  options.disk_path = home != nullptr && *home != '\0' ? home : "/";
  if (!parse(argc, argv, options)) {
    std::cerr << "statwell: invalid arguments (see --help)\n";
    return 2;
  }

  statwell::CpuProbe cpu;
  statwell::NetworkProbe network(options.interface_name);
  std::optional<statwell::Result<statwell::CpuSample>> first_cpu;
  std::optional<statwell::Result<statwell::NetworkSample>> first_network;
  if (options.selected[static_cast<std::size_t>(Metric::cpu)]) {
    first_cpu = cpu.sample();
  }
  if (options.selected[static_cast<std::size_t>(Metric::network)]) {
    first_network = network.sample();
  }
  if (options.selected[static_cast<std::size_t>(Metric::cpu)] ||
      options.selected[static_cast<std::size_t>(Metric::network)]) {
    std::this_thread::sleep_for(std::chrono::milliseconds(options.interval_ms));
  }

  std::vector<Entry> entries;
  entries.reserve(kNames.size());
  if (options.selected[static_cast<std::size_t>(Metric::cpu)]) {
    auto result = cpu.sample();
    if (!result && first_cpu && !*first_cpu &&
        first_cpu->error().code != statwell::ErrorCode::unavailable) {
      result = std::unexpected(first_cpu->error());
    }
    entries.push_back(entry("cpu", result, [](auto &fields, const auto &sample) {
      fields.push_back({"user_percent", decimal(sample.user_percent)});
      fields.push_back({"system_percent", decimal(sample.system_percent)});
      fields.push_back({"total_percent", decimal(sample.total_percent)});
    }));
  }
  if (options.selected[static_cast<std::size_t>(Metric::memory)]) {
    entries.push_back(
        entry("memory", statwell::sample_memory(), [](auto &fields, const auto &sample) {
          fields.push_back({"total_bytes", std::to_string(sample.total_bytes)});
          fields.push_back({"used_bytes", std::to_string(sample.used_bytes)});
          fields.push_back({"available_bytes", std::to_string(sample.available_bytes)});
          fields.push_back(
              {"pressure", std::string(statwell::pressure_name(sample.pressure)), true});
        }));
  }
  if (options.selected[static_cast<std::size_t>(Metric::load)]) {
    entries.push_back(entry("load", statwell::sample_load(), [](auto &fields, const auto &sample) {
      fields.push_back({"one_minute", decimal(sample.one_minute)});
      fields.push_back({"five_minutes", decimal(sample.five_minutes)});
      fields.push_back({"fifteen_minutes", decimal(sample.fifteen_minutes)});
    }));
  }
  if (options.selected[static_cast<std::size_t>(Metric::disk)]) {
    entries.push_back(entry(
        "disk", statwell::sample_disk(options.disk_path), [](auto &fields, const auto &sample) {
          fields.push_back({"total_bytes", std::to_string(sample.total_bytes)});
          fields.push_back({"available_bytes", std::to_string(sample.available_bytes)});
        }));
  }
  if (options.selected[static_cast<std::size_t>(Metric::battery)]) {
    entries.push_back(
        entry("battery", statwell::sample_battery(), [](auto &fields, const auto &sample) {
          fields.push_back({"percent", std::to_string(sample.percent)});
          fields.push_back({"charging", sample.charging ? "true" : "false"});
          fields.push_back({"external_power", sample.external_power ? "true" : "false"});
        }));
  }
  if (options.selected[static_cast<std::size_t>(Metric::network)]) {
    auto result = network.sample();
    if (!result && first_network && !*first_network &&
        first_network->error().code != statwell::ErrorCode::unavailable) {
      result = std::unexpected(first_network->error());
    }
    entries.push_back(entry("network", result, [](auto &fields, const auto &sample) {
      fields.push_back({"download_bytes_per_second", decimal(sample.download_bytes_per_second)});
      fields.push_back({"upload_bytes_per_second", decimal(sample.upload_bytes_per_second)});
    }));
  }

  const auto now = std::chrono::system_clock::now();
  const auto timestamp =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
  if (options.json)
    print_json(entries, timestamp);
  else
    print_kv(entries, timestamp);
  for (const auto &metric : entries) {
    if (metric.ok)
      return 0;
  }
  return 1;
}

} // namespace

int main(int argc, char **argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception &error) {
    std::cerr << "statwell: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "statwell: unexpected failure\n";
    return 1;
  }
}
