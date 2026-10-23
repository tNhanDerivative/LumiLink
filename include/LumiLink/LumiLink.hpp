#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "LumiLinkLogCallback.h"

/// Public C++ API of LumiLink.
/**
 * C++ clients (desktop apps, iOS, tests) link the static library `lumilink_core`
 * and use this header directly. Other languages go through the C API in
 * LumiLinkCAPI.h, which is a thin wrapper around this class and must not
 * contain any business logic.
 */
namespace lumilink {

/// Every operation returns one of these codes. Values are mirrored by LL_* in LumiLinkCAPI.h.
enum class Result : int {
  Ok = 0,
  NotConnected = 1,       ///< No device connected to the current device manager
  WriteFailed = 2,        ///< The device manager failed to send the packet
  NoSuchGroup = 3,
  GroupExists = 4,
  LedAlreadyInGroup = 5,
  LedNotInGroup = 6,
  NoSuchManager = 7,
  DeviceNotFound = 8,     ///< Scan finished without finding a LumiLink device
  ConnectFailed = 9,
  HandshakeFailed = 10,   ///< Device answered with an unsupported protocol version (or not at all)
  ScanFailed = 11,        ///< Bluetooth adapter unavailable or scan error
};

/// Human-readable name of a result code. Never returns nullptr.
const char* ToString(Result result);

struct Color {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
};

/// Effects are executed by the device firmware; the library only sends the order.
enum class Effect : std::uint8_t {
  None = 0,
  Blink = 1,
  Breathe = 2,
  Rainbow = 3,
};

/// LED index addressing every LED of the device at once.
constexpr std::uint8_t kAllLeds = 0xFF;

enum class LogMode : int {
  StdOut = 0,    ///< Info to std::cout, errors to std::cerr (default)
  File = 1,      ///< Append to the log file (default ./LumiLinkLog.txt)
  Callback = 2,  ///< Forward to the registered LumiLinkLogCallback
  Silent = 3,    ///< No output at all, including errors
};

/// Entry point of the library. One instance drives one device.
/**
 * A new instance starts with the "TextManager" device manager, which prints
 * every order instead of sending it. Switch to "BTManager" then call
 * FindDevice() to talk to a real device.
 *
 * All methods are thread-safe.
 */
class LumiLink {
public:
  LumiLink();
  ~LumiLink();

  LumiLink(const LumiLink&) = delete;
  LumiLink& operator=(const LumiLink&) = delete;

  /// Device managers: "TextManager", "RawManager", "BTManager".
  Result ChangeDeviceManager(const std::string& name);
  std::string DeviceManagerName() const;

  /// Asks the current device manager to find and connect to a device.
  Result FindDevice();
  bool HasDevice() const;
  /// Ok if a device is connected, NotConnected otherwise.
  Result Status() const;

  /// Sends raw bytes to the device, bypassing the packet protocol.
  Result Write(const std::vector<std::uint8_t>& data);

  Result SetColor(std::uint8_t led, Color color);
  Result SetBrightness(std::uint8_t led, std::uint8_t brightness);
  /// speed: 0 (slowest) - 255 (fastest). color is ignored by Effect::Rainbow.
  Result SetEffect(std::uint8_t led, Effect effect, std::uint8_t speed, Color color = {});
  Result TurnOff(std::uint8_t led);

  /// Light groups: named sets of LEDs controlled together.
  Result NewGroup(const std::string& name);
  Result AddToGroup(const std::string& name, std::uint8_t led);
  Result RemoveFromGroup(const std::string& name, std::uint8_t led);

  /// Group orders stop at the first LED that fails and return its error.
  Result SetGroupColor(const std::string& name, Color color);
  Result SetGroupBrightness(const std::string& name, std::uint8_t brightness);
  Result SetGroupEffect(const std::string& name, Effect effect, std::uint8_t speed, Color color = {});
  Result TurnOffGroup(const std::string& name);

  /// Logging is global to the process and can be configured before any instance exists.
  static void SetLogMode(LogMode mode);
  static LogMode GetLogMode();
  static void SetLogFile(const std::string& path);
  static void SetLogCallback(LumiLinkLogCallback callback);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lumilink
