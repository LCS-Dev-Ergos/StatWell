//===---------------------------------------------------------------------------===//
/**
 * @file probes_test.cpp
 * @author LCS.Dev - StatWell
 * @brief Native interface lookup regression on the running host.
 * @version 0.1
 * @date 2026-09-29
 *
 * @copyright MIT License 2026
 */
//===---------------------------------------------------------------------------===//

#include "statwell/probes.hpp"

#include <gtest/gtest.h>
#include <ifaddrs.h>
#include <net/if.h>

#include <memory>
#include <string>

namespace statwell {
namespace {

TEST(Platform, ReadsHighestActiveInterfaceIndex) {
  ifaddrs* raw = nullptr;
  ASSERT_EQ(getifaddrs(&raw), 0);
  const std::unique_ptr<ifaddrs, decltype(&freeifaddrs)> interfaces(raw, freeifaddrs);
  unsigned int                                           index = 0;
  std::string                                            name;
  for (auto* current = interfaces.get(); current != nullptr; current = current->ifa_next) {
    if (current->ifa_name == nullptr || (current->ifa_flags & IFF_UP) == 0)
      continue;
    const auto candidate = if_nametoindex(current->ifa_name);
    if (candidate > index) {
      index = candidate;
      name  = current->ifa_name;
    }
  }
  ASSERT_GT(index, 0U);
  const auto counters = read_network_counters(name);
  ASSERT_TRUE(counters) << name << " (index " << index << ")";
}

} // namespace
} // namespace statwell

//===---------------------------------------------------------------------------===//
