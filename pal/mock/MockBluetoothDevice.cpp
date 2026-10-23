#include "MockBluetoothDevice.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>

#include "BluetoothError.hpp"

namespace lumilink::pal {

namespace {

std::string ToHex(const std::uint8_t* data, std::size_t length)
{
  std::string out;
  char byte[4];
  for (std::size_t i = 0; i < length; ++i) {
    std::snprintf(byte, sizeof(byte), i == 0 ? "%02X" : " %02X", data[i]);
    out += byte;
  }
  return out;
}

}  // namespace

std::unique_ptr<IBluetoothDevice> CreateBluetoothDevice()
{
  MockBluetoothDevice::Options options;
  options.echo = std::getenv("LUMILINK_MOCK_ECHO") != nullptr;
  return std::make_unique<MockBluetoothDevice>(options);
}

MockBluetoothDevice::MockBluetoothDevice() : MockBluetoothDevice(Options{}) {}

MockBluetoothDevice::MockBluetoothDevice(Options options) : options_(std::move(options)) {}

std::vector<DeviceInfo> MockBluetoothDevice::Scan(std::chrono::milliseconds /*timeout*/)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (options_.failScan)
    throw BluetoothError("mock: Bluetooth adapter is off");
  return options_.advertised;
}

void MockBluetoothDevice::Connect(const std::string& address)
{
  std::lock_guard<std::mutex> lock(mutex_);
  const auto found = std::find_if(options_.advertised.begin(), options_.advertised.end(),
                                  [&](const DeviceInfo& d) { return d.address == address; });
  if (found == options_.advertised.end())
    throw BluetoothError("mock: unknown address " + address);
  if (options_.failConnect)
    throw BluetoothError("mock: connection refused by " + address);
  connected_ = true;
  address_ = address;
  pendingReply_.clear();
  if (options_.echo)
    std::cerr << "[mock-device] connected to " << found->name << " (" << address << ")" << std::endl;
}

void MockBluetoothDevice::Close() noexcept
{
  std::lock_guard<std::mutex> lock(mutex_);
  connected_ = false;
  address_.clear();
  pendingReply_.clear();
}

bool MockBluetoothDevice::IsConnected() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return connected_;
}

void MockBluetoothDevice::Write(const std::uint8_t* data, std::size_t length)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!connected_)
    throw BluetoothError("mock: write on a closed connection");
  if (options_.failWrite)
    throw BluetoothError("mock: write failed");
  written_.emplace_back(data, data + length);
  if (options_.echo)
    std::cerr << "[mock-device] rx: " << ToHex(data, length) << std::endl;
  Respond(data, length);
}

std::size_t MockBluetoothDevice::Read(std::uint8_t* buffer, std::size_t length,
                                      std::chrono::milliseconds /*timeout*/)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!connected_)
    throw BluetoothError("mock: read on a closed connection");
  // Replies are produced synchronously by Write(), so there is never anything to wait for.
  std::size_t count = 0;
  while (count < length && !pendingReply_.empty()) {
    buffer[count++] = pendingReply_.front();
    pendingReply_.pop_front();
  }
  return count;
}

void MockBluetoothDevice::Respond(const std::uint8_t* data, std::size_t length)
{
  // Handshake: ['H'][min api][max api] -> ['H'][api]
  if (length == 3 && data[0] == 'H' && options_.handshakeApi != 0) {
    pendingReply_.push_back('H');
    pendingReply_.push_back(options_.handshakeApi);
  }
}

std::vector<std::vector<std::uint8_t>> MockBluetoothDevice::Written() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return written_;
}

std::string MockBluetoothDevice::ConnectedAddress() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return address_;
}

void MockBluetoothDevice::SetFailWrite(bool fail)
{
  std::lock_guard<std::mutex> lock(mutex_);
  options_.failWrite = fail;
}

}  // namespace lumilink::pal
