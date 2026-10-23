#pragma once

#include "Command.hpp"

namespace lumilink {

/// Transport used to reach the LED device.
/**
 * Each subclass is a different way of delivering orders: printing them
 * (TextManager), dumping raw packets (RawManager) or sending them over
 * Bluetooth (BTManager). Callers are expected to serialize access (LumiLink
 * holds a mutex around every call).
 */
class DeviceManager {
public:
  virtual ~DeviceManager();

  virtual const char* Name() const = 0;

  /// Finds and connects to the device. See subclasses for the possible results.
  virtual Result FindDevice() = 0;
  virtual bool HasDevice() const = 0;

  /// Ok, NotConnected or WriteFailed.
  virtual Result Write(const Bytes& data) = 0;

  /// Default: encode the command with Packet::Encode and Write() it.
  virtual Result Execute(const Command& command);
};

}  // namespace lumilink
