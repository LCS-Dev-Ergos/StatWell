//===---------------------------------------------------------------------------===//
/**
 * @file runtime.hpp
 * @author LCS.Dev - StatWell
 * @brief Shared sampler, owner-only snapshot transport, and watcher interface.
 * @version 0.1
 * @date 2026-09-28
 *
 * @copyright MIT License 2026
 */
//===---------------------------------------------------------------------------===//

#pragma once

#ifndef STATWELL_RUNTIME_HPP
#define STATWELL_RUNTIME_HPP

#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace statwell {

/** @brief Runtime settings shared by daemon and one-shot fallback. */
struct RuntimeOptions {
  std::string               disk_path;
  std::string               interface_name;
  std::string               runtime_dir;
  std::string               homebrew_bin     = "/opt/homebrew/bin/brew";
  std::string               checkupdates_bin = "/usr/bin/checkupdates";
  std::chrono::milliseconds package_timeout{10'000};
  std::set<std::string>     providers;

  std::map<std::string, std::chrono::milliseconds> cadence_overrides;
};

/** @brief Returns a private default runtime directory for the current user. */
[[nodiscard]] std::string default_runtime_dir();

/** @brief Run the foreground daemon until SIGINT or SIGTERM. */
int run_daemon(const RuntimeOptions& options);

/** @brief Read a version-1 snapshot, or nullopt if the daemon has not published one. */
[[nodiscard]] std::optional<std::string> read_snapshot(std::string_view runtime_dir);

/** @brief Coalesce a refresh request for one enabled package provider. */
void request_refresh(std::string_view runtime_dir, std::string_view provider);

/** @brief Produce a version-1 snapshot when the daemon is absent. */
[[nodiscard]] std::string one_shot_snapshot(const RuntimeOptions& options);

/** @brief Follow one metric's sequence and push its fields to SketchyBar. */
int run_watch(const RuntimeOptions& options, std::string_view metric, std::string_view event);

} // namespace statwell

#endif // STATWELL_RUNTIME_HPP

//===---------------------------------------------------------------------------===//
