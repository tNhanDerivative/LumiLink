// Public C API + platform boundary.
//
// Rules for this file:
//  - No business logic: every function converts its arguments, forwards to
//    lumilink::LumiLink and converts the result back.
//  - No exception may leave a function: each body is wrapped in try/catch and
//    unknown failures become LL_ERR_EXCEPTION.

#include "LumiLinkCAPI.h"

#include <exception>
#include <string>

#include "Log.hpp"
#include "LumiLink/LumiLink.hpp"

using lumilink::Color;
using lumilink::Effect;
using lumilink::LogMode;
using lumilink::Result;

struct LumiLinkHandle {
  lumilink::LumiLink lib;
};

namespace {

static_assert(LL_OK == static_cast<int>(Result::Ok), "result codes out of sync");
static_assert(LL_ERR_NOT_CONNECTED == static_cast<int>(Result::NotConnected), "result codes out of sync");
static_assert(LL_ERR_WRITE_FAILED == static_cast<int>(Result::WriteFailed), "result codes out of sync");
static_assert(LL_ERR_NO_SUCH_GROUP == static_cast<int>(Result::NoSuchGroup), "result codes out of sync");
static_assert(LL_ERR_GROUP_EXISTS == static_cast<int>(Result::GroupExists), "result codes out of sync");
static_assert(LL_ERR_LED_ALREADY_IN_GROUP == static_cast<int>(Result::LedAlreadyInGroup), "result codes out of sync");
static_assert(LL_ERR_LED_NOT_IN_GROUP == static_cast<int>(Result::LedNotInGroup), "result codes out of sync");
static_assert(LL_ERR_NO_SUCH_MANAGER == static_cast<int>(Result::NoSuchManager), "result codes out of sync");
static_assert(LL_ERR_DEVICE_NOT_FOUND == static_cast<int>(Result::DeviceNotFound), "result codes out of sync");
static_assert(LL_ERR_CONNECT_FAILED == static_cast<int>(Result::ConnectFailed), "result codes out of sync");
static_assert(LL_ERR_HANDSHAKE_FAILED == static_cast<int>(Result::HandshakeFailed), "result codes out of sync");
static_assert(LL_ERR_SCAN_FAILED == static_cast<int>(Result::ScanFailed), "result codes out of sync");

static_assert(LL_EFFECT_NONE == static_cast<int>(Effect::None), "effects out of sync");
static_assert(LL_EFFECT_BLINK == static_cast<int>(Effect::Blink), "effects out of sync");
static_assert(LL_EFFECT_BREATHE == static_cast<int>(Effect::Breathe), "effects out of sync");
static_assert(LL_EFFECT_RAINBOW == static_cast<int>(Effect::Rainbow), "effects out of sync");

static_assert(LL_LOG_STDOUT == static_cast<int>(LogMode::StdOut), "log modes out of sync");
static_assert(LL_LOG_FILE == static_cast<int>(LogMode::File), "log modes out of sync");
static_assert(LL_LOG_CALLBACK == static_cast<int>(LogMode::Callback), "log modes out of sync");
static_assert(LL_LOG_SILENT == static_cast<int>(LogMode::Silent), "log modes out of sync");

static_assert(LL_ALL_LEDS == lumilink::kAllLeds, "LED broadcast index out of sync");

/// Must be called from a catch block. Logs the exception and returns LL_ERR_EXCEPTION.
int HandleException() noexcept
{
  try {
    try {
      throw;
    }
    catch (const std::exception& e) {
      lumilink::Log::Error(std::string("Unhandled exception at the C API boundary: ") + e.what());
    }
    catch (...) {
      lumilink::Log::Error("Unknown exception at the C API boundary");
    }
  }
  catch (...) {
    // Logging itself failed (out of memory...). Nothing else can be done safely.
  }
  return LL_ERR_EXCEPTION;
}

int ToInt(Result result)
{
  return static_cast<int>(result);
}

bool ToEffect(int value, Effect& effect)
{
  if (value < LL_EFFECT_NONE || value > LL_EFFECT_RAINBOW)
    return false;
  effect = static_cast<Effect>(value);
  return true;
}

const char* ManagerName(int manager)
{
  switch (manager) {
  case LL_MANAGER_TEXT: return "TextManager";
  case LL_MANAGER_RAW: return "RawManager";
  case LL_MANAGER_BT: return "BTManager";
  default: return nullptr;
  }
}

}  // namespace

extern "C" {

LUMILINK_API LumiLinkHandle* ll_create(void)
try {
  return new LumiLinkHandle();
}
catch (...) {
  HandleException();
  return nullptr;
}

LUMILINK_API void ll_destroy(LumiLinkHandle* handle)
try {
  delete handle;
}
catch (...) {
  HandleException();
}

LUMILINK_API int ll_change_device_manager(LumiLinkHandle* handle, int manager)
try {
  if (handle == nullptr)
    return LL_ERR_INVALID_HANDLE;
  const char* name = ManagerName(manager);
  if (name == nullptr)
    return LL_ERR_INVALID_ARG;
  return ToInt(handle->lib.ChangeDeviceManager(name));
}
catch (...) {
  return HandleException();
}

LUMILINK_API int ll_find_device(LumiLinkHandle* handle)
try {
  if (handle == nullptr)
    return LL_ERR_INVALID_HANDLE;
  return ToInt(handle->lib.FindDevice());
}
catch (...) {
  return HandleException();
}

LUMILINK_API int ll_has_device(LumiLinkHandle* handle)
try {
  if (handle == nullptr)
    return LL_ERR_INVALID_HANDLE;
  return handle->lib.HasDevice() ? 1 : 0;
}
catch (...) {
  return HandleException();
}

LUMILINK_API int ll_status(LumiLinkHandle* handle)
try {
  if (handle == nullptr)
    return LL_ERR_INVALID_HANDLE;
  return ToInt(handle->lib.Status());
}
catch (...) {
  return HandleException();
}

LUMILINK_API int ll_write(LumiLinkHandle* handle, const uint8_t* data, int length)
try {
  if (handle == nullptr)
    return LL_ERR_INVALID_HANDLE;
  if (length < 0 || (data == nullptr && length > 0))
    return LL_ERR_INVALID_ARG;
  const std::vector<std::uint8_t> bytes(data, data + length);
  return ToInt(handle->lib.Write(bytes));
}
catch (...) {
  return HandleException();
}

LUMILINK_API int ll_set_color(LumiLinkHandle* handle, uint8_t led, uint8_t r, uint8_t g, uint8_t b)
try {
  if (handle == nullptr)
    return LL_ERR_INVALID_HANDLE;
  return ToInt(handle->lib.SetColor(led, Color{r, g, b}));
}
catch (...) {
  return HandleException();
}

LUMILINK_API int ll_set_brightness(LumiLinkHandle* handle, uint8_t led, uint8_t brightness)
try {
  if (handle == nullptr)
    return LL_ERR_INVALID_HANDLE;
  return ToInt(handle->lib.SetBrightness(led, brightness));
}
catch (...) {
  return HandleException();
}

LUMILINK_API int ll_set_effect(LumiLinkHandle* handle, uint8_t led, int effect, uint8_t speed,
                               uint8_t r, uint8_t g, uint8_t b)
try {
  if (handle == nullptr)
    return LL_ERR_INVALID_HANDLE;
  Effect cppEffect;
  if (!ToEffect(effect, cppEffect))
    return LL_ERR_INVALID_ARG;
  return ToInt(handle->lib.SetEffect(led, cppEffect, speed, Color{r, g, b}));
}
catch (...) {
  return HandleException();
}

LUMILINK_API int ll_turn_off(LumiLinkHandle* handle, uint8_t led)
try {
  if (handle == nullptr)
    return LL_ERR_INVALID_HANDLE;
  return ToInt(handle->lib.TurnOff(led));
}
catch (...) {
  return HandleException();
}

LUMILINK_API int ll_new_group(LumiLinkHandle* handle, const char* name)
try {
  if (handle == nullptr)
    return LL_ERR_INVALID_HANDLE;
  if (name == nullptr)
    return LL_ERR_INVALID_ARG;
  return ToInt(handle->lib.NewGroup(name));
}
catch (...) {
  return HandleException();
}

LUMILINK_API int ll_add_to_group(LumiLinkHandle* handle, const char* name, uint8_t led)
try {
  if (handle == nullptr)
    return LL_ERR_INVALID_HANDLE;
  if (name == nullptr)
    return LL_ERR_INVALID_ARG;
  return ToInt(handle->lib.AddToGroup(name, led));
}
catch (...) {
  return HandleException();
}

LUMILINK_API int ll_remove_from_group(LumiLinkHandle* handle, const char* name, uint8_t led)
try {
  if (handle == nullptr)
    return LL_ERR_INVALID_HANDLE;
  if (name == nullptr)
    return LL_ERR_INVALID_ARG;
  return ToInt(handle->lib.RemoveFromGroup(name, led));
}
catch (...) {
  return HandleException();
}

LUMILINK_API int ll_set_group_color(LumiLinkHandle* handle, const char* name, uint8_t r, uint8_t g, uint8_t b)
try {
  if (handle == nullptr)
    return LL_ERR_INVALID_HANDLE;
  if (name == nullptr)
    return LL_ERR_INVALID_ARG;
  return ToInt(handle->lib.SetGroupColor(name, Color{r, g, b}));
}
catch (...) {
  return HandleException();
}

LUMILINK_API int ll_set_group_brightness(LumiLinkHandle* handle, const char* name, uint8_t brightness)
try {
  if (handle == nullptr)
    return LL_ERR_INVALID_HANDLE;
  if (name == nullptr)
    return LL_ERR_INVALID_ARG;
  return ToInt(handle->lib.SetGroupBrightness(name, brightness));
}
catch (...) {
  return HandleException();
}

LUMILINK_API int ll_set_group_effect(LumiLinkHandle* handle, const char* name, int effect, uint8_t speed,
                                     uint8_t r, uint8_t g, uint8_t b)
try {
  if (handle == nullptr)
    return LL_ERR_INVALID_HANDLE;
  Effect cppEffect;
  if (name == nullptr || !ToEffect(effect, cppEffect))
    return LL_ERR_INVALID_ARG;
  return ToInt(handle->lib.SetGroupEffect(name, cppEffect, speed, Color{r, g, b}));
}
catch (...) {
  return HandleException();
}

LUMILINK_API int ll_turn_off_group(LumiLinkHandle* handle, const char* name)
try {
  if (handle == nullptr)
    return LL_ERR_INVALID_HANDLE;
  if (name == nullptr)
    return LL_ERR_INVALID_ARG;
  return ToInt(handle->lib.TurnOffGroup(name));
}
catch (...) {
  return HandleException();
}

LUMILINK_API int ll_set_log_mode(int mode)
try {
  if (mode < LL_LOG_STDOUT || mode > LL_LOG_SILENT)
    return LL_ERR_INVALID_ARG;
  lumilink::LumiLink::SetLogMode(static_cast<LogMode>(mode));
  return LL_OK;
}
catch (...) {
  return HandleException();
}

LUMILINK_API int ll_get_log_mode(void)
try {
  return static_cast<int>(lumilink::LumiLink::GetLogMode());
}
catch (...) {
  return HandleException();
}

LUMILINK_API int ll_set_log_file(const char* path)
try {
  if (path == nullptr)
    return LL_ERR_INVALID_ARG;
  lumilink::LumiLink::SetLogFile(path);
  return LL_OK;
}
catch (...) {
  return HandleException();
}

LUMILINK_API void ll_set_log_callback(LumiLinkLogCallback callback)
try {
  lumilink::LumiLink::SetLogCallback(callback);
}
catch (...) {
  HandleException();
}

LUMILINK_API const char* ll_result_string(int result)
{
  switch (result) {
  case LL_ERR_INVALID_HANDLE: return "invalid handle";
  case LL_ERR_INVALID_ARG: return "invalid argument";
  case LL_ERR_EXCEPTION: return "internal error";
  default:
    if (result < LL_OK || result > LL_ERR_SCAN_FAILED)
      return "unknown result";
    return lumilink::ToString(static_cast<Result>(result));
  }
}

}  // extern "C"
