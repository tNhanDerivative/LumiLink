#include "RawManager.hpp"

#include <iostream>

#include "Packet.hpp"

namespace lumilink {

RawManager::RawManager() : RawManager(std::cout) {}

RawManager::RawManager(std::ostream& out) : out_(out) {}

Result RawManager::FindDevice()
{
  Write(Packet::Handshake(Packet::kApiVersion, Packet::kApiVersion));
  return Result::Ok;
}

bool RawManager::HasDevice() const
{
  return true;
}

Result RawManager::Write(const Bytes& data)
{
  out_.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  out_.flush();
  return out_ ? Result::Ok : Result::WriteFailed;
}

}  // namespace lumilink
