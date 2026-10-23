// BTManager against the mock PAL: connection flow, handshake and error mapping.

#include "BTManager.hpp"
#include "Log.hpp"
#include "Packet.hpp"
#include "TestUtil.hpp"
#include "mock/MockBluetoothDevice.hpp"

using namespace lumilink;
using pal::MockBluetoothDevice;

namespace {

struct Fixture {
  explicit Fixture(MockBluetoothDevice::Options options = {})
  {
    auto device = std::make_unique<MockBluetoothDevice>(std::move(options));
    mock = device.get();  // owned by manager, valid for the fixture's lifetime
    manager = std::make_unique<BTManager>(std::move(device));
  }

  MockBluetoothDevice* mock = nullptr;
  std::unique_ptr<BTManager> manager;
};

void TestConnectAndSend()
{
  Fixture f;
  CHECK(!f.manager->HasDevice());
  CHECK_EQ(f.manager->Execute(Command::MakeTurnOff(0)), Result::NotConnected);

  CHECK_EQ(f.manager->FindDevice(), Result::Ok);
  CHECK(f.manager->HasDevice());
  CHECK_EQ(f.mock->ConnectedAddress(), std::string("00:11:22:33:44:55"));

  const Command order = Command::MakeColor(2, Color{10, 20, 30});
  CHECK_EQ(f.manager->Execute(order), Result::Ok);

  const auto written = f.mock->Written();
  CHECK_EQ(written.size(), 2u);
  if (written.size() == 2) {
    CHECK_EQ(written[0], Packet::Handshake(1, 1));
    CHECK_EQ(written[1], Packet::Encode(order));
  }

  f.mock->SetFailWrite(true);
  CHECK_EQ(f.manager->Execute(order), Result::WriteFailed);
}

void TestSelectsDeviceByName()
{
  MockBluetoothDevice::Options options;
  options.advertised = {{"AA", "Headphones"}, {"BB", "LumiLink-Desk"}};
  Fixture f(options);
  CHECK_EQ(f.manager->FindDevice(), Result::Ok);
  CHECK_EQ(f.mock->ConnectedAddress(), std::string("BB"));
}

void TestErrors()
{
  {
    MockBluetoothDevice::Options options;
    options.advertised = {{"AA", "Headphones"}};
    Fixture f(options);
    CHECK_EQ(f.manager->FindDevice(), Result::DeviceNotFound);
    CHECK(!f.manager->HasDevice());
  }
  {
    MockBluetoothDevice::Options options;
    options.failScan = true;
    Fixture f(options);
    CHECK_EQ(f.manager->FindDevice(), Result::ScanFailed);
  }
  {
    MockBluetoothDevice::Options options;
    options.failConnect = true;
    Fixture f(options);
    CHECK_EQ(f.manager->FindDevice(), Result::ConnectFailed);
    CHECK(!f.manager->HasDevice());
  }
  {
    MockBluetoothDevice::Options options;
    options.handshakeApi = 2;  // device speaks a protocol we don't
    Fixture f(options);
    CHECK_EQ(f.manager->FindDevice(), Result::HandshakeFailed);
    CHECK(!f.manager->HasDevice());
  }
  {
    MockBluetoothDevice::Options options;
    options.handshakeApi = 0;  // device never answers
    Fixture f(options);
    CHECK_EQ(f.manager->FindDevice(), Result::HandshakeFailed);
    CHECK(!f.manager->HasDevice());
  }
}

void TestReconnect()
{
  Fixture f;
  CHECK_EQ(f.manager->FindDevice(), Result::Ok);
  CHECK_EQ(f.manager->FindDevice(), Result::Ok);
  CHECK(f.manager->HasDevice());
}

}  // namespace

int main()
{
  Log::SetMode(LogMode::Silent);
  TestConnectAndSend();
  TestSelectsDeviceByName();
  TestErrors();
  TestReconnect();
  return lumilink_test::Finish("btmanager_tests");
}
