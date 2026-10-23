#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "DeviceManager.hpp"
#include "IBluetoothDevice.hpp"

namespace lumilink {

struct BTManagerSettings {
  std::string namePrefix = "LumiLink";  ///< Connect to the first device whose name starts with this
  std::chrono::milliseconds scanTimeout{5000};
  std::chrono::milliseconds handshakeTimeout{2000};
};

/// Sends orders to a LumiLink device over Bluetooth through the PAL.
/**
 * The Bluetooth stack is only reached through pal::IBluetoothDevice, so this
 * class is identical on every platform. Any pal::BluetoothError is caught here
 * and converted into a Result.
 */
class BTManager : public DeviceManager {
public:
  using Settings = BTManagerSettings;

  /// Uses the PAL implementation selected at build time.
  BTManager();
  /// Injects any PAL implementation (mock devices in tests).
  explicit BTManager(std::unique_ptr<pal::IBluetoothDevice> device, Settings settings = {});
  ~BTManager() override;

  const char* Name() const override { return "BTManager"; }

  /// Ok, ScanFailed, DeviceNotFound, ConnectFailed or HandshakeFailed.
  Result FindDevice() override;
  bool HasDevice() const override;
  Result Write(const Bytes& data) override;

private:
  Result Handshake();

  std::unique_ptr<pal::IBluetoothDevice> device_;
  Settings settings_;
};

}  // namespace lumilink
