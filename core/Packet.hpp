#pragma once

#include <cstddef>
#include <string>

#include "Command.hpp"

/// Builds and parses the byte packets understood by LumiLink firmware.
/**
 * Handshake (3 bytes):  ['H'][min api][max api]
 *   Expected reply:     ['H'][api], with min <= api <= max
 *
 * Order: header ['L'][api version][data length] followed by data:
 *   SetColor       ['C'][led][r][g][b]
 *   SetBrightness  ['B'][led][brightness]
 *   SetEffect      ['E'][led][effect][speed][r][g][b]
 *   TurnOff        ['O'][led]
 * led = 0xFF addresses every LED.
 */
namespace lumilink::Packet {

constexpr std::uint8_t kApiVersion = 1;
constexpr std::uint8_t kHandshakeTag = 'H';
constexpr std::uint8_t kOrderTag = 'L';

Bytes Encode(const Command& command);
Bytes Handshake(std::uint8_t minApi, std::uint8_t maxApi);

/// Returns the negotiated API version, or 0 if the reply is invalid.
std::uint8_t HandshakeResult(const std::uint8_t* reply, std::size_t length, std::uint8_t minApi,
                             std::uint8_t maxApi);

/// "4C 01 05 ..." formatting, for logs.
std::string ToHex(const Bytes& bytes);

}  // namespace lumilink::Packet
