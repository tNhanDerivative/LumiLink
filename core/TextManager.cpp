#include "TextManager.hpp"

#include "Log.hpp"
#include "Packet.hpp"

namespace lumilink {

Result TextManager::FindDevice()
{
  return Result::Ok;
}

bool TextManager::HasDevice() const
{
  return true;
}

Result TextManager::Write(const Bytes& data)
{
  Log::Info("Wrote " + std::to_string(data.size()) + " bytes: " + Packet::ToHex(data), true);
  return Result::Ok;
}

Result TextManager::Execute(const Command& command)
{
  Log::Info(Describe(command) + ". Packet: " + Packet::ToHex(Packet::Encode(command)), true);
  return Result::Ok;
}

}  // namespace lumilink
