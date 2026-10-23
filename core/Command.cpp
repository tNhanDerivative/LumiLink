#include "Command.hpp"

#include <cstdio>
#include <sstream>

namespace lumilink {

namespace {

std::string LedName(std::uint8_t led)
{
  return led == kAllLeds ? std::string("all LEDs") : "LED " + std::to_string(led);
}

std::string ColorHex(Color color)
{
  char buffer[8];
  std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", color.r, color.g, color.b);
  return buffer;
}

}  // namespace

Command Command::MakeColor(std::uint8_t led, Color color)
{
  Command command;
  command.op = Op::SetColor;
  command.led = led;
  command.color = color;
  return command;
}

Command Command::MakeBrightness(std::uint8_t led, std::uint8_t brightness)
{
  Command command;
  command.op = Op::SetBrightness;
  command.led = led;
  command.brightness = brightness;
  return command;
}

Command Command::MakeEffect(std::uint8_t led, Effect effect, std::uint8_t speed, Color color)
{
  Command command;
  command.op = Op::SetEffect;
  command.led = led;
  command.effect = effect;
  command.speed = speed;
  command.color = color;
  return command;
}

Command Command::MakeTurnOff(std::uint8_t led)
{
  Command command;
  command.op = Op::TurnOff;
  command.led = led;
  return command;
}

const char* ToString(Effect effect)
{
  switch (effect) {
  case Effect::None: return "none";
  case Effect::Blink: return "blink";
  case Effect::Breathe: return "breathe";
  case Effect::Rainbow: return "rainbow";
  }
  return "unknown";
}

std::string Describe(const Command& command)
{
  std::ostringstream ss;
  ss << LedName(command.led);
  switch (command.op) {
  case Op::SetColor:
    ss << " color " << ColorHex(command.color);
    break;
  case Op::SetBrightness:
    ss << " brightness " << static_cast<int>(command.brightness);
    break;
  case Op::SetEffect:
    ss << " effect " << ToString(command.effect) << " speed " << static_cast<int>(command.speed)
       << " color " << ColorHex(command.color);
    break;
  case Op::TurnOff:
    ss << " off";
    break;
  }
  return ss.str();
}

}  // namespace lumilink
