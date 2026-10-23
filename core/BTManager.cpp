#include "BTManager.hpp"

#include "BluetoothError.hpp"
#include "Log.hpp"
#include "Packet.hpp"

namespace lumilink {

BTManager::BTManager() : BTManager(pal::CreateBluetoothDevice()) {}

BTManager::BTManager(std::unique_ptr<pal::IBluetoothDevice> device, Settings settings)
    : device_(std::move(device)), settings_(std::move(settings))
{
}

BTManager::~BTManager()
{
  if (device_)
    device_->Close();
}

Result BTManager::FindDevice()
{
  device_->Close();

  Log::Info("Scanning for Bluetooth devices named '" + settings_.namePrefix + "*'");
  std::vector<pal::DeviceInfo> devices;
  try {
    devices = device_->Scan(settings_.scanTimeout);
  }
  catch (const pal::BluetoothError& e) {
    Log::Error(std::string("Bluetooth scan failed. Is the adapter on? ") + e.what());
    return Result::ScanFailed;
  }

  for (const auto& info : devices) {
    Log::Info("  found: " + info.name + " (" + info.address + ")");
    if (info.name.compare(0, settings_.namePrefix.size(), settings_.namePrefix) != 0)
      continue;

    try {
      device_->Connect(info.address);
    }
    catch (const pal::BluetoothError& e) {
      Log::Error("Connection to " + info.address + " failed: " + e.what());
      return Result::ConnectFailed;
    }

    const Result handshake = Handshake();
    if (handshake != Result::Ok) {
      device_->Close();
      return handshake;
    }
    Log::Info("Connected to " + info.name);
    return Result::Ok;
  }
  return Result::DeviceNotFound;
}

Result BTManager::Handshake()
{
  if (Write(Packet::Handshake(Packet::kApiVersion, Packet::kApiVersion)) != Result::Ok) {
    Log::Error("Could not send the handshake");
    return Result::HandshakeFailed;
  }

  std::uint8_t reply[2] = {0, 0};
  std::size_t received = 0;
  try {
    while (received < sizeof(reply)) {
      const std::size_t n =
          device_->Read(reply + received, sizeof(reply) - received, settings_.handshakeTimeout);
      if (n == 0)
        break;  // timeout
      received += n;
    }
  }
  catch (const pal::BluetoothError& e) {
    Log::Error(std::string("Could not read the handshake reply: ") + e.what());
    return Result::HandshakeFailed;
  }

  if (Packet::HandshakeResult(reply, received, Packet::kApiVersion, Packet::kApiVersion) == 0) {
    Log::Error("Handshake failed: the device does not speak protocol v" +
               std::to_string(Packet::kApiVersion));
    return Result::HandshakeFailed;
  }
  return Result::Ok;
}

bool BTManager::HasDevice() const
{
  return device_->IsConnected();
}

Result BTManager::Write(const Bytes& data)
{
  if (!device_->IsConnected())
    return Result::NotConnected;
  try {
    device_->Write(data.data(), data.size());
  }
  catch (const pal::BluetoothError& e) {
    Log::Error(std::string("Bluetooth write failed: ") + e.what());
    return Result::WriteFailed;
  }
  return Result::Ok;
}

}  // namespace lumilink
