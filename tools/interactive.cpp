// Interactive command line to drive a LumiLink device by hand.
// Not a stable interface: do not script against it.

#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "LumiLink/LumiLink.hpp"

using namespace lumilink;

namespace {

const char* const kHelp = R"(Commands:
  manager <text|raw|bt>                 switch device manager
  find                                  find and connect to a device
  status                                show connection status
  color   <led|all> <r> <g> <b>         set LED color (0-255)
  bright  <led|all> <0-255>             set LED brightness
  effect  <led|all> <none|blink|breathe|rainbow> <speed> [r g b]
  off     <led|all>                     turn LED off
  group new <name>
  group add|rm <name> <led>
  group color  <name> <r> <g> <b>
  group bright <name> <0-255>
  group effect <name> <effect> <speed> [r g b]
  group off    <name>
  log <stdout|silent|file <path>>       change log output
  help | quit)";

bool ParseByte(const std::string& token, std::uint8_t& value)
{
  try {
    std::size_t used = 0;
    const int parsed = std::stoi(token, &used);
    if (used != token.size() || parsed < 0 || parsed > 255)
      return false;
    value = static_cast<std::uint8_t>(parsed);
    return true;
  }
  catch (...) {
    return false;
  }
}

bool ParseLed(const std::string& token, std::uint8_t& led)
{
  if (token == "all") {
    led = kAllLeds;
    return true;
  }
  return ParseByte(token, led);
}

bool ParseEffect(const std::string& token, Effect& effect)
{
  static const std::map<std::string, Effect> effects{
      {"none", Effect::None}, {"blink", Effect::Blink}, {"breathe", Effect::Breathe}, {"rainbow", Effect::Rainbow}};
  const auto it = effects.find(token);
  if (it == effects.end())
    return false;
  effect = it->second;
  return true;
}

/// Parses args[first..first+2] as a color; missing values default to 0.
bool ParseColor(const std::vector<std::string>& args, std::size_t first, Color& color, bool optional)
{
  if (args.size() < first + 3)
    return optional && args.size() == first;
  return ParseByte(args[first], color.r) && ParseByte(args[first + 1], color.g) &&
         ParseByte(args[first + 2], color.b);
}

void Report(Result result)
{
  std::cout << (result == Result::Ok ? "OK" : "ERROR") << " (" << ToString(result) << ")" << std::endl;
}

/// Returns false when the arguments are invalid.
bool RunGroup(LumiLink& lib, const std::vector<std::string>& args)
{
  if (args.size() < 3)
    return false;
  const std::string& sub = args[1];
  const std::string& name = args[2];
  std::uint8_t value = 0;
  Color color;
  Effect effect = Effect::None;

  if (sub == "new" && args.size() == 3)
    Report(lib.NewGroup(name));
  else if (sub == "add" && args.size() == 4 && ParseLed(args[3], value))
    Report(lib.AddToGroup(name, value));
  else if (sub == "rm" && args.size() == 4 && ParseLed(args[3], value))
    Report(lib.RemoveFromGroup(name, value));
  else if (sub == "color" && ParseColor(args, 3, color, false))
    Report(lib.SetGroupColor(name, color));
  else if (sub == "bright" && args.size() == 4 && ParseByte(args[3], value))
    Report(lib.SetGroupBrightness(name, value));
  else if (sub == "effect" && args.size() >= 5 && ParseEffect(args[3], effect) && ParseByte(args[4], value) &&
           ParseColor(args, 5, color, true))
    Report(lib.SetGroupEffect(name, effect, value, color));
  else if (sub == "off" && args.size() == 3)
    Report(lib.TurnOffGroup(name));
  else
    return false;
  return true;
}

/// Returns false when the arguments are invalid.
bool Run(LumiLink& lib, const std::vector<std::string>& args)
{
  const std::string& cmd = args[0];
  std::uint8_t led = 0;
  std::uint8_t value = 0;
  Color color;
  Effect effect = Effect::None;

  if (cmd == "help") {
    std::cout << kHelp << std::endl;
  }
  else if (cmd == "manager" && args.size() == 2) {
    static const std::map<std::string, std::string> managers{
        {"text", "TextManager"}, {"raw", "RawManager"}, {"bt", "BTManager"}};
    const auto it = managers.find(args[1]);
    if (it == managers.end())
      return false;
    Report(lib.ChangeDeviceManager(it->second));
  }
  else if (cmd == "find" && args.size() == 1) {
    std::cout << "Searching with " << lib.DeviceManagerName() << "..." << std::endl;
    Report(lib.FindDevice());
  }
  else if (cmd == "status" && args.size() == 1) {
    std::cout << lib.DeviceManagerName() << ": " << (lib.HasDevice() ? "connected" : "not connected")
              << std::endl;
  }
  else if (cmd == "color" && args.size() == 5 && ParseLed(args[1], led) && ParseColor(args, 2, color, false)) {
    Report(lib.SetColor(led, color));
  }
  else if (cmd == "bright" && args.size() == 3 && ParseLed(args[1], led) && ParseByte(args[2], value)) {
    Report(lib.SetBrightness(led, value));
  }
  else if (cmd == "effect" && args.size() >= 4 && ParseLed(args[1], led) && ParseEffect(args[2], effect) &&
           ParseByte(args[3], value) && ParseColor(args, 4, color, true)) {
    Report(lib.SetEffect(led, effect, value, color));
  }
  else if (cmd == "off" && args.size() == 2 && ParseLed(args[1], led)) {
    Report(lib.TurnOff(led));
  }
  else if (cmd == "group") {
    return RunGroup(lib, args);
  }
  else if (cmd == "log" && args.size() == 2 && args[1] == "stdout") {
    LumiLink::SetLogMode(LogMode::StdOut);
  }
  else if (cmd == "log" && args.size() == 2 && args[1] == "silent") {
    LumiLink::SetLogMode(LogMode::Silent);
  }
  else if (cmd == "log" && args.size() == 3 && args[1] == "file") {
    LumiLink::SetLogFile(args[2]);
    LumiLink::SetLogMode(LogMode::File);
  }
  else {
    return false;
  }
  return true;
}

}  // namespace

int main()
{
  LumiLink lib;
  std::cout << "LumiLink interactive shell. Type 'help' for the list of commands." << std::endl;

  std::string line;
  while (std::cout << "> " << std::flush && std::getline(std::cin, line)) {
    std::istringstream stream(line);
    std::vector<std::string> args;
    for (std::string token; stream >> token;)
      args.push_back(token);
    if (args.empty())
      continue;
    if (args[0] == "quit" || args[0] == "exit")
      break;
    if (!Run(lib, args))
      std::cout << "Invalid command, type 'help'." << std::endl;
  }
  return 0;
}
