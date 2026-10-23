#include "DeviceManager.hpp"
#include "GroupManager.hpp"
#include "LumiLink/LumiLink.hpp"
#include "TestUtil.hpp"

using namespace lumilink;

namespace {

/// Records the orders it receives; can be told to fail from the Nth order.
class RecordingManager : public DeviceManager {
public:
  const char* Name() const override { return "RecordingManager"; }
  Result FindDevice() override { return Result::Ok; }
  bool HasDevice() const override { return true; }
  Result Write(const Bytes&) override { return Result::Ok; }
  Result Execute(const Command& command) override
  {
    if (failFrom >= 0 && static_cast<int>(commands.size()) >= failFrom)
      return Result::WriteFailed;
    commands.push_back(command);
    return Result::Ok;
  }

  std::vector<Command> commands;
  int failFrom = -1;
};

void TestPublicApi()
{
  LumiLink lib;
  CHECK_EQ(lib.NewGroup("ring"), Result::Ok);
  CHECK_EQ(lib.NewGroup("ring"), Result::GroupExists);
  CHECK_EQ(lib.AddToGroup("ring", 0), Result::Ok);
  CHECK_EQ(lib.AddToGroup("ring", 1), Result::Ok);
  CHECK_EQ(lib.AddToGroup("ring", 0), Result::LedAlreadyInGroup);
  CHECK_EQ(lib.SetGroupColor("ring", Color{255, 0, 0}), Result::Ok);
  CHECK_EQ(lib.SetGroupBrightness("ring", 10), Result::Ok);
  CHECK_EQ(lib.SetGroupEffect("ring", Effect::Blink, 5), Result::Ok);
  CHECK_EQ(lib.TurnOffGroup("ring"), Result::Ok);
  CHECK_EQ(lib.SetGroupColor("missing", Color{}), Result::NoSuchGroup);
  CHECK_EQ(lib.AddToGroup("missing", 0), Result::NoSuchGroup);
  CHECK_EQ(lib.RemoveFromGroup("missing", 0), Result::NoSuchGroup);
  CHECK_EQ(lib.RemoveFromGroup("ring", 42), Result::LedNotInGroup);
  CHECK_EQ(lib.RemoveFromGroup("ring", 1), Result::Ok);
}

void TestFanOut()
{
  GroupManager groups;
  RecordingManager device;
  CHECK_EQ(groups.NewGroup("strip"), Result::Ok);
  CHECK_EQ(groups.AddToGroup("strip", 4), Result::Ok);
  CHECK_EQ(groups.AddToGroup("strip", 2), Result::Ok);
  CHECK_EQ(groups.AddToGroup("strip", 9), Result::Ok);
  CHECK_EQ(groups.RemoveFromGroup("strip", 2), Result::Ok);

  CHECK_EQ(groups.Execute("strip", Command::MakeColor(0, Color{1, 2, 3}), device), Result::Ok);
  CHECK_EQ(device.commands.size(), 2u);
  if (device.commands.size() == 2) {
    // One order per LED, in insertion order, with the group's payload
    CHECK_EQ(device.commands[0].led, 4);
    CHECK_EQ(device.commands[1].led, 9);
    CHECK(device.commands[1].op == Op::SetColor);
    CHECK_EQ(device.commands[1].color.b, 3);
  }

  // Empty group: nothing sent, still Ok
  CHECK_EQ(groups.NewGroup("empty"), Result::Ok);
  device.commands.clear();
  CHECK_EQ(groups.Execute("empty", Command::MakeTurnOff(0), device), Result::Ok);
  CHECK(device.commands.empty());

  // Stops at the first failing LED
  device.failFrom = 1;
  CHECK_EQ(groups.Execute("strip", Command::MakeTurnOff(0), device), Result::WriteFailed);
  CHECK_EQ(device.commands.size(), 1u);
}

}  // namespace

int main()
{
  LumiLink::SetLogMode(LogMode::Silent);
  TestPublicApi();
  TestFanOut();
  return lumilink_test::Finish("group_tests");
}
