// Contract test for pal::IBluetoothDevice.
//
// Written against the interface only, so the very same suite runs against the
// mock and against every native implementation. Native PALs must be tested on
// their own platform, with a LumiLink device powered on and in range.

#include <algorithm>
#include <chrono>

#include "BluetoothError.hpp"
#include "IBluetoothDevice.hpp"
#include "TestUtil.hpp"

using namespace lumilink::pal;
using namespace std::chrono_literals;

namespace {

bool IsLumiLink(const DeviceInfo& info)
{
  return info.name.rfind("LumiLink", 0) == 0;
}

}  // namespace

int main()
{
  std::unique_ptr<IBluetoothDevice> device = CreateBluetoothDevice();
  CHECK(device != nullptr);
  if (!device)
    return lumilink_test::Finish("pal_tests");

  // Nothing works before a connection is open
  CHECK(!device->IsConnected());
  const std::uint8_t ping[] = {'H', 1, 1};
  std::uint8_t reply[2] = {0, 0};
  CHECK_THROWS(device->Write(ping, sizeof(ping)), BluetoothError);
  CHECK_THROWS(device->Read(reply, sizeof(reply), 100ms), BluetoothError);

  // Close is always safe
  device->Close();
  device->Close();

  // Scan finds a LumiLink device
  const std::vector<DeviceInfo> devices = device->Scan(5s);
  const auto found = std::find_if(devices.begin(), devices.end(), IsLumiLink);
  CHECK(found != devices.end());
  if (found == devices.end())
    return lumilink_test::Finish("pal_tests");

  // Unknown address
  CHECK_THROWS(device->Connect("not-a-device-address"), BluetoothError);
  CHECK(!device->IsConnected());

  // Connect + handshake round trip
  device->Connect(found->address);
  CHECK(device->IsConnected());
  device->Write(ping, sizeof(ping));
  std::size_t received = 0;
  while (received < sizeof(reply)) {
    const std::size_t n = device->Read(reply + received, sizeof(reply) - received, 2s);
    if (n == 0)
      break;
    received += n;
  }
  CHECK_EQ(received, sizeof(reply));
  CHECK_EQ(reply[0], 'H');
  CHECK_EQ(reply[1], 1);

  // Close ends the connection
  device->Close();
  CHECK(!device->IsConnected());
  CHECK_THROWS(device->Write(ping, sizeof(ping)), BluetoothError);

  return lumilink_test::Finish("pal_tests");
}
