#include "GroupManager.hpp"

#include "DeviceManager.hpp"

namespace lumilink {

Result GroupManager::NewGroup(const std::string& name)
{
  const bool inserted = groups_.emplace(name, LightGroup(name)).second;
  return inserted ? Result::Ok : Result::GroupExists;
}

Result GroupManager::AddToGroup(const std::string& name, std::uint8_t led)
{
  const auto it = groups_.find(name);
  if (it == groups_.end())
    return Result::NoSuchGroup;
  return it->second.Add(led);
}

Result GroupManager::RemoveFromGroup(const std::string& name, std::uint8_t led)
{
  const auto it = groups_.find(name);
  if (it == groups_.end())
    return Result::NoSuchGroup;
  return it->second.Remove(led);
}

Result GroupManager::Execute(const std::string& name, Command command, DeviceManager& device) const
{
  const auto it = groups_.find(name);
  if (it == groups_.end())
    return Result::NoSuchGroup;
  for (const std::uint8_t led : it->second.Leds()) {
    command.led = led;
    const Result result = device.Execute(command);
    if (result != Result::Ok)
      return result;
  }
  return Result::Ok;
}

}  // namespace lumilink
