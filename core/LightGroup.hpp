#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "LumiLink/LumiLink.hpp"

namespace lumilink {

/// A named set of LEDs that receive the same orders.
class LightGroup {
public:
  explicit LightGroup(std::string name);

  /// Ok or LedAlreadyInGroup.
  Result Add(std::uint8_t led);
  /// Ok or LedNotInGroup.
  Result Remove(std::uint8_t led);

  const std::string& Name() const { return name_; }
  const std::vector<std::uint8_t>& Leds() const { return leds_; }

private:
  std::string name_;
  std::vector<std::uint8_t> leds_;  ///< Kept in insertion order: orders are sent in that order
};

}  // namespace lumilink
