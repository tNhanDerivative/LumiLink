#include "LightGroup.hpp"

#include <algorithm>

namespace lumilink {

LightGroup::LightGroup(std::string name) : name_(std::move(name)) {}

Result LightGroup::Add(std::uint8_t led)
{
  if (std::find(leds_.begin(), leds_.end(), led) != leds_.end())
    return Result::LedAlreadyInGroup;
  leds_.push_back(led);
  return Result::Ok;
}

Result LightGroup::Remove(std::uint8_t led)
{
  const auto it = std::find(leds_.begin(), leds_.end(), led);
  if (it == leds_.end())
    return Result::LedNotInGroup;
  leds_.erase(it);
  return Result::Ok;
}

}  // namespace lumilink
