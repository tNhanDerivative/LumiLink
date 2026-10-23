#include "Packet.hpp"

#include <cstdio>

namespace lumilink::Packet {

namespace {

Bytes WithHeader(const Bytes& data)
{
  Bytes packet{kOrderTag, kApiVersion, static_cast<std::uint8_t>(data.size())};
  packet.insert(packet.end(), data.begin(), data.end());
  return packet;
}

}  // namespace

Bytes Encode(const Command& command)
{
  const auto op = static_cast<std::uint8_t>(command.op);
  switch (command.op) {
  case Op::SetColor:
    return WithHeader({op, command.led, command.color.r, command.color.g, command.color.b});
  case Op::SetBrightness:
    return WithHeader({op, command.led, command.brightness});
  case Op::SetEffect:
    return WithHeader({op, command.led, static_cast<std::uint8_t>(command.effect), command.speed,
                       command.color.r, command.color.g, command.color.b});
  case Op::TurnOff:
    return WithHeader({op, command.led});
  }
  return {};
}

Bytes Handshake(std::uint8_t minApi, std::uint8_t maxApi)
{
  return {kHandshakeTag, minApi, maxApi};
}

std::uint8_t HandshakeResult(const std::uint8_t* reply, std::size_t length, std::uint8_t minApi,
                             std::uint8_t maxApi)
{
  if (reply == nullptr || length < 2 || reply[0] != kHandshakeTag)
    return 0;
  const std::uint8_t api = reply[1];
  return api >= minApi && api <= maxApi ? api : 0;
}

std::string ToHex(const Bytes& bytes)
{
  std::string out;
  char byte[4];
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    std::snprintf(byte, sizeof(byte), i == 0 ? "%02X" : " %02X", bytes[i]);
    out += byte;
  }
  return out;
}

}  // namespace lumilink::Packet
