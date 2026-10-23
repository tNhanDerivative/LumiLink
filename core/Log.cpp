#include "Log.hpp"

#include <fstream>
#include <iostream>
#include <mutex>

namespace lumilink::Log {

namespace {

constexpr bool kVerbose = LUMILINK_VERBOSE != 0;  // set by CMake option LUMILINK_VERBOSE

enum Level { kInfo = 0, kError = 1 };

struct State {
  std::mutex mutex;
  LogMode mode = LogMode::StdOut;
  std::string file = "./LumiLinkLog.txt";
  LumiLinkLogCallback callback = nullptr;
};

// Leaked on purpose: logging must keep working during static destruction.
State& GetState()
{
  static State* state = new State();
  return *state;
}

void Emit(Level level, const std::string& message)
{
  State& state = GetState();
  LumiLinkLogCallback callback = nullptr;
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    switch (state.mode) {
    case LogMode::StdOut:
      (level == kError ? std::cerr : std::cout)
          << (level == kError ? "[LumiLink.ERR] " : "[LumiLink.LOG] ") << message << std::endl;
      return;
    case LogMode::File: {
      std::ofstream out(state.file, std::ios_base::app);
      out << (level == kError ? "[LumiLink.ERR] " : "[LumiLink.LOG] ") << message << std::endl;
      return;
    }
    case LogMode::Callback:
      callback = state.callback;
      break;
    case LogMode::Silent:
      return;
    }
  }
  // Called outside the lock so the callback may safely call back into the library.
  if (callback != nullptr)
    callback(level, message.c_str());
}

}  // namespace

void SetMode(LogMode mode)
{
  State& state = GetState();
  std::lock_guard<std::mutex> lock(state.mutex);
  switch (mode) {
  case LogMode::StdOut:
  case LogMode::File:
  case LogMode::Callback:
  case LogMode::Silent:
    state.mode = mode;
    break;
  default:
    state.mode = LogMode::StdOut;
    break;
  }
}

LogMode GetMode()
{
  State& state = GetState();
  std::lock_guard<std::mutex> lock(state.mutex);
  return state.mode;
}

void SetFile(const std::string& path)
{
  State& state = GetState();
  std::lock_guard<std::mutex> lock(state.mutex);
  state.file = path;
}

void SetCallback(LumiLinkLogCallback callback)
{
  State& state = GetState();
  std::lock_guard<std::mutex> lock(state.mutex);
  state.callback = callback;
}

void Info(const std::string& message, bool force)
{
  if (kVerbose || force)
    Emit(kInfo, message);
}

void Error(const std::string& message)
{
  Emit(kError, message);
}

}  // namespace lumilink::Log
