//===---------------------------------------------------------------------------===//
/**
 * @file linux_parsers.cpp
 * @author LCS.Dev - StatWell
 * @brief Strict allocation-free parsing of bounded Linux metric records.
 * @version 0.1
 * @date 2026-09-28
 *
 * @copyright MIT License 2026
 */
//===---------------------------------------------------------------------------===//

#include "linux_parsers.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <limits>

namespace statwell::detail {
namespace {

constexpr ProbeError kInvalid{ErrorCode::invalid_input};

[[nodiscard]] constexpr bool whitespace(char value) noexcept {
  return value == ' ' || value == '\t' || value == '\n' || value == '\r';
}

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
  while (!text.empty() && whitespace(text.front()))
    text.remove_prefix(1);
  while (!text.empty() && whitespace(text.back()))
    text.remove_suffix(1);
  return text;
}

[[nodiscard]] std::string_view first_line(std::string_view& text) noexcept {
  const auto end  = text.find('\n');
  auto       line = text;
  if (end == std::string_view::npos) {
    text = {};
  } else {
    line.remove_suffix(line.size() - end);
    text.remove_prefix(end + 1);
  }
  return line;
}

[[nodiscard]] std::string_view next_token(std::string_view& text) noexcept {
  text = trim(text);
  if (text.empty())
    return {};
  const auto end   = text.find_first_of(" \t\r\n");
  auto       token = text;
  if (end == std::string_view::npos) {
    text = {};
  } else {
    token.remove_suffix(token.size() - end);
    text.remove_prefix(end);
  }
  return token;
}

[[nodiscard]] bool unsigned_integer(std::string_view text, std::uint64_t& value) noexcept {
  if (text.empty())
    return false;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  return error == std::errc{} && end == text.data() + text.size();
}

[[nodiscard]] bool add(std::uint64_t& total, std::uint64_t value) noexcept {
  if (value > std::numeric_limits<std::uint64_t>::max() - total)
    return false;
  total += value;
  return true;
}

[[nodiscard]] Pressure parse_psi(std::string_view text) noexcept {
  while (!text.empty()) {
    auto line = first_line(text);
    if (next_token(line) != "some")
      continue;
    while (true) {
      const auto token = next_token(line);
      if (token.empty())
        break;
      if (!token.starts_with("avg10="))
        continue;
      auto value = token;
      value.remove_prefix(6);
      const auto dot                = value.find('.');
      auto       whole              = value;
      bool       fractional_nonzero = false;
      if (dot != std::string_view::npos) {
        whole.remove_suffix(whole.size() - dot);
        value.remove_prefix(dot + 1);
        if (value.empty())
          return Pressure::unknown;
        for (const char digit : value) {
          if (digit < '0' || digit > '9')
            return Pressure::unknown;
          fractional_nonzero |= digit != '0';
        }
      }
      std::uint64_t percent = 0;
      if (!unsigned_integer(whole, percent) || percent > 100 || (percent == 100 && fractional_nonzero))
        return Pressure::unknown;
      if (percent >= 10)
        return Pressure::critical;
      if (percent >= 1)
        return Pressure::warning;
      return Pressure::normal;
    }
  }
  return Pressure::unknown;
}

} // namespace

//===----- PROCFS AND SYSFS PARSERS --------------------------------------------===//

Result<CpuTicks> parse_proc_stat(std::string_view text) noexcept {
  auto line = first_line(text);
  if (next_token(line) != "cpu")
    return std::unexpected(kInvalid);

  std::array<std::uint64_t, 8> fields{};
  std::size_t                  count = 0;
  while (true) {
    const auto token = next_token(line);
    if (token.empty())
      break;
    std::uint64_t value = 0;
    if (!unsigned_integer(token, value))
      return std::unexpected(kInvalid);
    if (count < fields.size())
      fields[count] = value;
    ++count;
  }
  if (count < 4)
    return std::unexpected(kInvalid);

  std::uint64_t system = fields[2];
  std::uint64_t idle   = fields[3];
  if (!add(system, fields[5]) || !add(system, fields[6]) || !add(idle, fields[4]) || !add(idle, fields[7]))
    return std::unexpected(kInvalid);
  return CpuTicks{fields[0], system, idle, fields[1]};
}

Result<MemorySample> parse_meminfo(const MemoryRecords& records) noexcept {
  auto          text            = records.meminfo;
  std::uint64_t total_kib       = 0;
  std::uint64_t available_kib   = 0;
  bool          found_total     = false;
  bool          found_available = false;
  while (!text.empty()) {
    auto       line         = first_line(text);
    const auto name         = next_token(line);
    const bool is_total     = name == "MemTotal:";
    const bool is_available = name == "MemAvailable:";
    if (!is_total && !is_available)
      continue;
    std::uint64_t value = 0;
    if (!unsigned_integer(next_token(line), value) || next_token(line) != "kB" || !next_token(line).empty()
        || value > std::numeric_limits<std::uint64_t>::max() / 1'024)
      return std::unexpected(kInvalid);
    if (is_total) {
      if (found_total)
        return std::unexpected(kInvalid);
      total_kib   = value;
      found_total = true;
    } else {
      if (found_available)
        return std::unexpected(kInvalid);
      available_kib   = value;
      found_available = true;
    }
  }
  if (!found_total || !found_available || total_kib == 0 || available_kib > total_kib)
    return std::unexpected(kInvalid);
  const auto total     = total_kib * 1'024;
  const auto available = available_kib * 1'024;
  return MemorySample{total, total - available, available, parse_psi(records.psi)};
}

Result<NetworkCounters> parse_net_dev(std::string_view text, std::string_view interface_name) noexcept {
  if (interface_name.empty())
    return std::unexpected(kInvalid);
  while (!text.empty()) {
    auto       line  = first_line(text);
    const auto colon = line.rfind(':');
    if (colon == std::string_view::npos)
      continue;
    auto name = line;
    name.remove_suffix(name.size() - colon);
    if (trim(name) != interface_name)
      continue;
    line.remove_prefix(colon + 1);
    std::array<std::uint64_t, 16> fields{};
    for (auto& field : fields) {
      if (!unsigned_integer(next_token(line), field))
        return std::unexpected(kInvalid);
    }
    if (!next_token(line).empty())
      return std::unexpected(kInvalid);
    return NetworkCounters{fields[0], fields[8]};
  }
  return std::unexpected(ProbeError{ErrorCode::unavailable});
}

Result<BatterySample> parse_battery(BatteryRecords records) noexcept {
  std::uint64_t percent = 0;
  if (!unsigned_integer(trim(records.capacity), percent) || percent > 100)
    return std::unexpected(kInvalid);
  const auto status = trim(records.status);
  if (status != "Charging" && status != "Discharging" && status != "Full" && status != "Not charging" && status != "Unknown")
    return std::unexpected(kInvalid);
  return BatterySample{static_cast<std::uint8_t>(percent), status == "Charging", records.external_power};
}

} // namespace statwell::detail

//===---------------------------------------------------------------------------===//
