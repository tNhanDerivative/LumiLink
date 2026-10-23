#pragma once

#include <deque>
#include <mutex>

#include "IBluetoothDevice.hpp"

namespace lumilink::pal {

/// In-memory LED device used until the native implementations exist.
/**
 * It behaves like a LumiLink firmware: it advertises a device, accepts a
 * connection and answers the handshake packet ['H'][min][max] with
 * ['H'][api]. Every packet written to it is recorded so tests can inspect it.
 */
class MockBluetoothDevice : public IBluetoothDevice {
public:
  struct Options {
    std::vector<DeviceInfo> advertised{{"00:11:22:33:44:55", "LumiLink-Mock"}};
    bool failScan = false;
    bool failConnect = false;
    bool failWrite = false;
    std::uint8_t handshakeApi = 1;  ///< API version reported by the device, 0 = never answer
    bool echo = false;              ///< Print every received packet on std::cerr
  };

  MockBluetoothDevice();
  explicit MockBluetoothDevice(Options options);

  std::vector<DeviceInfo> Scan(std::chrono::milliseconds timeout) override;
  void Connect(const std::string& address) override;
  void Close() noexcept override;
  bool IsConnected() const override;
  void Write(const std::uint8_t* data, std::size_t length) override;
  std::size_t Read(std::uint8_t* buffer, std::size_t length, std::chrono::milliseconds timeout) override;

  /// Test helpers
  std::vector<std::vector<std::uint8_t>> Written() const;
  std::string ConnectedAddress() const;
  void SetFailWrite(bool fail);

private:
  void Respond(const std::uint8_t* data, std::size_t length);

  mutable std::mutex mutex_;
  Options options_;
  bool connected_ = false;
  std::string address_;
  std::vector<std::vector<std::uint8_t>> written_;
  std::deque<std::uint8_t> pendingReply_;
};

}  // namespace lumilink::pal
