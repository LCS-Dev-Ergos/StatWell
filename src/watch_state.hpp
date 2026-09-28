//===---------------------------------------------------------------------------===//
/**
 * @file watch_state.hpp
 * @author LCS.Dev - StatWell
 * @brief Event identity for changed and expired SketchyBar samples.
 * @version 0.1
 * @date 2026-09-28
 *
 * @copyright MIT License 2026
 */
//===---------------------------------------------------------------------------===//

#pragma once

#ifndef STATWELL_WATCH_STATE_HPP
#define STATWELL_WATCH_STATE_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace statwell::detail {

/** @brief Change identity once more when an unchanged value has expired. */
[[nodiscard]] std::string event_identity(
    std::string_view instance, std::string_view sequence, std::int64_t value_at_ms, std::int64_t max_age_ms, std::int64_t now_ms);

} // namespace statwell::detail

#endif // STATWELL_WATCH_STATE_HPP

//===---------------------------------------------------------------------------===//
