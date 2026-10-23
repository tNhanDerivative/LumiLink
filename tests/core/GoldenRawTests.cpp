// Golden master test: the byte stream produced by RawManager for a fixed
// scenario must match tests/golden/raw_sequence.bin exactly. Any protocol change
// shows up here.
//
// After an intentional protocol change, regenerate the file with:
//   LUMILINK_UPDATE_GOLDEN=1 ./golden_raw_tests

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>

#include "GroupManager.hpp"
#include "Log.hpp"
#include "Packet.hpp"
#include "RawManager.hpp"
#include "TestUtil.hpp"

using namespace lumilink;

namespace {

const char* const kGoldenFile = LUMILINK_GOLDEN_DIR "/raw_sequence.bin";

std::string RunScenario()
{
  std::ostringstream out;
  RawManager raw(out);
  GroupManager groups;

  raw.FindDevice();
  raw.Execute(Command::MakeColor(0, Color{255, 0, 0}));
  raw.Execute(Command::MakeColor(kAllLeds, Color{0, 0, 0}));
  raw.Execute(Command::MakeBrightness(1, 128));
  raw.Execute(Command::MakeEffect(2, Effect::Blink, 10, Color{0, 255, 0}));
  raw.Execute(Command::MakeEffect(kAllLeds, Effect::Rainbow, 200, Color{}));
  groups.NewGroup("pair");
  groups.AddToGroup("pair", 3);
  groups.AddToGroup("pair", 4);
  groups.Execute("pair", Command::MakeEffect(0, Effect::Breathe, 30, Color{0, 0, 255}), raw);
  groups.Execute("pair", Command::MakeTurnOff(0), raw);
  raw.Execute(Command::MakeTurnOff(kAllLeds));
  return out.str();
}

}  // namespace

int main()
{
  Log::SetMode(LogMode::Silent);
  const std::string actual = RunScenario();

  if (std::getenv("LUMILINK_UPDATE_GOLDEN") != nullptr) {
    std::ofstream(kGoldenFile, std::ios::binary) << actual;
    std::cout << "Golden master updated: " << kGoldenFile << std::endl;
    return 0;
  }

  std::ifstream file(kGoldenFile, std::ios::binary);
  CHECK(file.good());
  const std::string expected((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  CHECK_EQ(actual.size(), expected.size());
  CHECK(actual == expected);
  if (actual != expected) {
    const auto toBytes = [](const std::string& s) { return Bytes(s.begin(), s.end()); };
    std::cerr << "expected: " << Packet::ToHex(toBytes(expected)) << "\n"
              << "actual:   " << Packet::ToHex(toBytes(actual)) << std::endl;
  }
  return lumilink_test::Finish("golden_raw_tests");
}
