#pragma once

#include <map>
#include <string>

#include "Command.hpp"
#include "LightGroup.hpp"

namespace lumilink {

class DeviceManager;

/// Owns the light groups and fans group orders out to every LED of a group.
class GroupManager {
public:
  /// Ok or GroupExists.
  Result NewGroup(const std::string& name);
  /// Ok, NoSuchGroup or LedAlreadyInGroup.
  Result AddToGroup(const std::string& name, std::uint8_t led);
  /// Ok, NoSuchGroup or LedNotInGroup.
  Result RemoveFromGroup(const std::string& name, std::uint8_t led);

  /// Sends `command` to every LED of the group (command.led is overwritten).
  /**
   * Returns NoSuchGroup, or the first error reported by the device manager.
   */
  Result Execute(const std::string& name, Command command, DeviceManager& device) const;

private:
  std::map<std::string, LightGroup> groups_;
};

}  // namespace lumilink
