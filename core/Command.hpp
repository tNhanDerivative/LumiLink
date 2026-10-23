#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "LumiLink/LumiLink.hpp"

namespace lumilink {

using Bytes = std::vector<std::uint8_t>;

/// Opcodes of the LED protocol, also used as the first data byte of a packet.
enum class Op : std::uint8_t {
  SetColor = 'C',
  SetBrightness = 'B',
  SetEffect = 'E',
  TurnOff = 'O',
};

/// One order for one LED (or kAllLeds), independent of how it is transported.
struct Command {
  Op op = Op::TurnOff;
  std::uint8_t led = 0;
  Color color{};
  std::uint8_t brightness = 0;
  Effect effect = Effect::None;
  std::uint8_t speed = 0;

  static Command MakeColor(std::uint8_t led, Color color);
  static Command MakeBrightness(std::uint8_t led, std::uint8_t brightness);
  static Command MakeEffect(std::uint8_t led, Effect effect, std::uint8_t speed, Color color);
  static Command MakeTurnOff(std::uint8_t led);
};

/// Human-readable description, e.g. "LED 3 color #FF8000".
std::string Describe(const Command& command);
const char* ToString(Effect effect);

}  // namespace lumilink
