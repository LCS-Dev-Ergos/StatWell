//===---------------------------------------------------------------------------===//
/**
 * @file packages.hpp
 * @author LCS.Dev - StatWell
 * @brief Bounded package-update probes for Homebrew and pacman.
 * @version 0.1
 * @date 2026-09-28
 *
 * @copyright MIT License 2026
 */
//===---------------------------------------------------------------------------===//

#pragma once

#ifndef STATWELL_PACKAGES_HPP
#define STATWELL_PACKAGES_HPP

#include "statwell/metrics.hpp"

#include <chrono>
#include <cstdint>
#include <stop_token>
#include <string_view>

namespace statwell {

enum class PackageKind : std::uint8_t { homebrew, pacman };

struct UpdateSample {
  std::uint32_t total         = 0;
  std::uint32_t formulae      = 0;
  std::uint32_t casks         = 0;
  bool          has_breakdown = false;
};

/** @brief Runs one fixed, unprivileged command with a bounded output and deadline. */
[[nodiscard]] Result<UpdateSample>
sample_updates(PackageKind kind, std::string_view executable, std::chrono::milliseconds timeout, const std::stop_token& stop = {});

} // namespace statwell

#endif // STATWELL_PACKAGES_HPP

//===---------------------------------------------------------------------------===//
