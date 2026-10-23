#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/// Platform Abstraction Layer: the only way the core talks to a Bluetooth stack.
/**
 * The core depends on this interface only. Each platform provides its own
 * implementation in pal/<platform>/ and the build system decides which file
 * is compiled (see CMakeLists.txt, option LUMILINK_PAL). No #ifdef here.
 *
 * Contract shared by every implementation (checked by tests/pal):
 *  - Failures are reported by throwing lumilink::pal::BluetoothError.
 *  - Write/Read on a closed connection throw BluetoothError.
 *  - Close() never throws and can be called any number of times.
 *  - Read() returns the number of bytes read, 0 when the timeout expires.
 */
namespace lumilink::pal {

struct DeviceInfo {
  std::string address;  ///< Platform identifier (MAC address, UUID on macOS, ...)
  std::string name;     ///< Advertised name
};

class IBluetoothDevice {
public:
  virtual ~IBluetoothDevice();

  virtual std::vector<DeviceInfo> Scan(std::chrono::milliseconds timeout) = 0;
  virtual void Connect(const std::string& address) = 0;
  virtual void Close() noexcept = 0;
  virtual bool IsConnected() const = 0;
  virtual void Write(const std::uint8_t* data, std::size_t length) = 0;
  virtual std::size_t Read(std::uint8_t* buffer, std::size_t length, std::chrono::milliseconds timeout) = 0;
};

/// Implemented once per platform; returns the implementation selected at build time.
std::unique_ptr<IBluetoothDevice> CreateBluetoothDevice();

}  // namespace lumilink::pal
