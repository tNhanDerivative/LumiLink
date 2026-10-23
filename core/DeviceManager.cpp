#include "DeviceManager.hpp"

#include "Packet.hpp"

namespace lumilink {

// Out-of-line so the vtable is emitted in exactly one translation unit.
DeviceManager::~DeviceManager() = default;

Result DeviceManager::Execute(const Command& command)
{
  return Write(Packet::Encode(command));
}

}  // namespace lumilink
