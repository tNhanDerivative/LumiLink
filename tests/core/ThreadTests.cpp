// Hammers one LumiLink instance from several threads, including device manager
// switches in the middle of the traffic. Passes if nothing crashes or deadlocks
// and every result is one the API documents for these calls.

#include <atomic>
#include <thread>
#include <vector>

#include "LumiLink/LumiLink.hpp"
#include "TestUtil.hpp"

using namespace lumilink;

namespace {

constexpr int kThreads = 8;
constexpr int kIterations = 500;

bool IsExpected(Result result)
{
  // NotConnected happens right after a switch to BTManager, before FindDevice
  return result == Result::Ok || result == Result::NotConnected;
}

}  // namespace

int main()
{
  LumiLink::SetLogMode(LogMode::Silent);

  LumiLink lib;
  CHECK_EQ(lib.ChangeDeviceManager("BTManager"), Result::Ok);
  CHECK_EQ(lib.FindDevice(), Result::Ok);
  CHECK_EQ(lib.NewGroup("all"), Result::Ok);
  for (std::uint8_t led = 0; led < 8; ++led)
    CHECK_EQ(lib.AddToGroup("all", led), Result::Ok);

  std::atomic<int> unexpected{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&lib, &unexpected, t] {
      for (int i = 0; i < kIterations; ++i) {
        const auto led = static_cast<std::uint8_t>(i % 8);
        const auto value = static_cast<std::uint8_t>(i);
        Result result = Result::Ok;
        switch ((t + i) % 5) {
        case 0: result = lib.SetColor(led, Color{value, 0, value}); break;
        case 1: result = lib.SetBrightness(led, value); break;
        case 2: result = lib.SetGroupEffect("all", Effect::Rainbow, value); break;
        case 3: result = lib.TurnOff(kAllLeds); break;
        case 4:
          if (i % 50 == 0) {
            lib.ChangeDeviceManager(i % 100 == 0 ? "TextManager" : "BTManager");
            result = lib.FindDevice();
          }
          break;
        }
        if (!IsExpected(result))
          ++unexpected;
      }
    });
  }
  for (auto& thread : threads)
    thread.join();

  CHECK_EQ(unexpected.load(), 0);
  return lumilink_test::Finish("thread_tests");
}
