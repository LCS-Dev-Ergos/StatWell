//===---------------------------------------------------------------------------===//
/**
 * @file registry.hpp
 * @author LCS.Dev - StatWell
 * @brief Type-erased metric boundary for the generic scheduler.
 * @version 0.1
 * @date 2026-09-28
 *
 * @copyright MIT License 2026
 */
//===---------------------------------------------------------------------------===//

#pragma once

#ifndef STATWELL_REGISTRY_HPP
#define STATWELL_REGISTRY_HPP

#include "statwell/runtime.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string_view>
#include <vector>

namespace statwell {

/** @brief One probe's state and serializer, independent of daemon scheduling. */
class MetricSource {
public:
  virtual ~MetricSource()                                                                                   = default;
  virtual void sample() noexcept                                                                            = 0;
  virtual void render(std::ostream& out, std::chrono::milliseconds cadence, std::int64_t duration_us) const = 0;

  virtual bool poll(std::int64_t&) noexcept { return false; }

  [[nodiscard]] virtual bool pending() const noexcept { return false; }

  [[nodiscard]] virtual bool failed() const noexcept { return false; }
};

/** @brief A registered probe with its independent monotonic deadline. */
struct Registration {
  std::string_view                      name;
  std::chrono::milliseconds             cadence;
  std::unique_ptr<MetricSource>         source;
  std::chrono::steady_clock::time_point next;
  std::int64_t                          duration_us    = 0;
  unsigned                              failures       = 0;
  bool                                  refresh_queued = false;
  bool                                  refresh_error  = false;
};

/** @brief Retry errors independently of the ordinary metric freshness budget. */
[[nodiscard]] std::chrono::seconds package_retry_delay(unsigned failures) noexcept;

/** @brief Construct built-in probes once; the scheduler treats them uniformly. */
[[nodiscard]] std::vector<Registration> make_registry(const RuntimeOptions& options);

} // namespace statwell

#endif // STATWELL_REGISTRY_HPP

//===---------------------------------------------------------------------------===//
