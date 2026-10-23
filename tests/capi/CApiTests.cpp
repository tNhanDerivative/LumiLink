// C API seen from a foreign client: only LumiLinkCAPI.h and the shared library.

#include <atomic>
#include <cstring>
#include <string>

#include "LumiLinkCAPI.h"
#include "TestUtil.hpp"

namespace {

constexpr bool kMockPal = LUMILINK_MOCK_PAL != 0;  // set by CMake

std::atomic<int> g_infoCount{0};
std::atomic<int> g_errorCount{0};

void LUMILINK_CALLBACK OnLog(int level, const char* message)
{
  if (message == nullptr)
    return;
  if (level == 0)
    ++g_infoCount;
  else
    ++g_errorCount;
}

void TestInvalidHandle()
{
  CHECK_EQ(ll_change_device_manager(nullptr, LL_MANAGER_TEXT), LL_ERR_INVALID_HANDLE);
  CHECK_EQ(ll_find_device(nullptr), LL_ERR_INVALID_HANDLE);
  CHECK_EQ(ll_has_device(nullptr), LL_ERR_INVALID_HANDLE);
  CHECK_EQ(ll_status(nullptr), LL_ERR_INVALID_HANDLE);
  CHECK_EQ(ll_write(nullptr, nullptr, 0), LL_ERR_INVALID_HANDLE);
  CHECK_EQ(ll_set_color(nullptr, 0, 1, 2, 3), LL_ERR_INVALID_HANDLE);
  CHECK_EQ(ll_set_brightness(nullptr, 0, 1), LL_ERR_INVALID_HANDLE);
  CHECK_EQ(ll_set_effect(nullptr, 0, LL_EFFECT_BLINK, 1, 0, 0, 0), LL_ERR_INVALID_HANDLE);
  CHECK_EQ(ll_turn_off(nullptr, 0), LL_ERR_INVALID_HANDLE);
  CHECK_EQ(ll_new_group(nullptr, "g"), LL_ERR_INVALID_HANDLE);
  CHECK_EQ(ll_turn_off_group(nullptr, "g"), LL_ERR_INVALID_HANDLE);
  ll_destroy(nullptr);  // must be a no-op
}

void TestInvalidArguments(LumiLinkHandle* h)
{
  CHECK_EQ(ll_change_device_manager(h, 42), LL_ERR_INVALID_ARG);
  CHECK_EQ(ll_set_effect(h, 0, 99, 1, 0, 0, 0), LL_ERR_INVALID_ARG);
  CHECK_EQ(ll_set_effect(h, 0, -1, 1, 0, 0, 0), LL_ERR_INVALID_ARG);
  CHECK_EQ(ll_new_group(h, nullptr), LL_ERR_INVALID_ARG);
  CHECK_EQ(ll_add_to_group(h, nullptr, 0), LL_ERR_INVALID_ARG);
  CHECK_EQ(ll_set_group_effect(h, "g", 99, 0, 0, 0, 0), LL_ERR_INVALID_ARG);
  CHECK_EQ(ll_write(h, nullptr, 3), LL_ERR_INVALID_ARG);
  CHECK_EQ(ll_write(h, nullptr, -1), LL_ERR_INVALID_ARG);
  CHECK_EQ(ll_set_log_mode(17), LL_ERR_INVALID_ARG);
  CHECK_EQ(ll_set_log_file(nullptr), LL_ERR_INVALID_ARG);
}

void TestTextManager(LumiLinkHandle* h)
{
  CHECK_EQ(ll_status(h), LL_OK);
  CHECK_EQ(ll_has_device(h), 1);
  CHECK_EQ(ll_set_color(h, 0, 255, 0, 0), LL_OK);
  CHECK_EQ(ll_set_brightness(h, LL_ALL_LEDS, 64), LL_OK);
  CHECK_EQ(ll_set_effect(h, 1, LL_EFFECT_BREATHE, 10, 0, 0, 255), LL_OK);
  CHECK_EQ(ll_turn_off(h, LL_ALL_LEDS), LL_OK);
  const uint8_t raw[] = {0x00, 0x01, 0x02};
  CHECK_EQ(ll_write(h, raw, 3), LL_OK);

  CHECK_EQ(ll_new_group(h, "pair"), LL_OK);
  CHECK_EQ(ll_new_group(h, "pair"), LL_ERR_GROUP_EXISTS);
  CHECK_EQ(ll_add_to_group(h, "pair", 1), LL_OK);
  CHECK_EQ(ll_add_to_group(h, "pair", 1), LL_ERR_LED_ALREADY_IN_GROUP);
  CHECK_EQ(ll_set_group_color(h, "pair", 1, 2, 3), LL_OK);
  CHECK_EQ(ll_set_group_brightness(h, "pair", 4), LL_OK);
  CHECK_EQ(ll_set_group_effect(h, "pair", LL_EFFECT_RAINBOW, 5, 0, 0, 0), LL_OK);
  CHECK_EQ(ll_turn_off_group(h, "pair"), LL_OK);
  CHECK_EQ(ll_remove_from_group(h, "pair", 7), LL_ERR_LED_NOT_IN_GROUP);
  CHECK_EQ(ll_turn_off_group(h, "nope"), LL_ERR_NO_SUCH_GROUP);
}

void TestBluetooth(LumiLinkHandle* h)
{
  CHECK_EQ(ll_change_device_manager(h, LL_MANAGER_BT), LL_OK);
  CHECK_EQ(ll_has_device(h), 0);
  CHECK_EQ(ll_status(h), LL_ERR_NOT_CONNECTED);
  CHECK_EQ(ll_set_color(h, 0, 1, 2, 3), LL_ERR_NOT_CONNECTED);
  CHECK_EQ(ll_find_device(h), LL_OK);
  CHECK_EQ(ll_has_device(h), 1);
  CHECK_EQ(ll_set_color(h, 0, 1, 2, 3), LL_OK);
  // Groups survive a device manager change
  CHECK_EQ(ll_set_group_color(h, "pair", 9, 9, 9), LL_OK);
  CHECK_EQ(ll_change_device_manager(h, LL_MANAGER_TEXT), LL_OK);
}

void TestLogging(LumiLinkHandle* h)
{
  CHECK_EQ(ll_get_log_mode(), LL_LOG_SILENT);
  ll_set_log_callback(&OnLog);
  CHECK_EQ(ll_set_log_mode(LL_LOG_CALLBACK), LL_OK);
  CHECK_EQ(ll_get_log_mode(), LL_LOG_CALLBACK);

  // TextManager always reports the orders it receives
  CHECK_EQ(ll_set_color(h, 3, 1, 2, 3), LL_OK);
  CHECK(g_infoCount.load() > 0);
  CHECK_EQ(g_errorCount.load(), 0);

  // Once unregistered, the callback is no longer called
  ll_set_log_callback(nullptr);
  const int before = g_infoCount.load();
  CHECK_EQ(ll_set_color(h, 3, 1, 2, 3), LL_OK);
  CHECK_EQ(g_infoCount.load(), before);
  CHECK_EQ(ll_set_log_mode(LL_LOG_SILENT), LL_OK);
}

void TestResultStrings()
{
  CHECK(std::strcmp(ll_result_string(LL_OK), "ok") == 0);
  CHECK(std::strcmp(ll_result_string(LL_ERR_INVALID_HANDLE), "invalid handle") == 0);
  CHECK(std::strcmp(ll_result_string(LL_ERR_HANDSHAKE_FAILED), "handshake failed") == 0);
  CHECK(std::strcmp(ll_result_string(12345), "unknown result") == 0);
}

}  // namespace

int main()
{
  CHECK_EQ(ll_set_log_mode(LL_LOG_SILENT), LL_OK);

  TestInvalidHandle();

  LumiLinkHandle* h = ll_create();
  CHECK(h != nullptr);
  if (h == nullptr)
    return lumilink_test::Finish("capi_tests");

  TestInvalidArguments(h);
  TestTextManager(h);
  if (kMockPal)
    TestBluetooth(h);
  TestLogging(h);
  TestResultStrings();

  // Two handles are independent
  LumiLinkHandle* other = ll_create();
  CHECK_EQ(ll_new_group(other, "pair"), LL_OK);
  ll_destroy(other);

  ll_destroy(h);
  return lumilink_test::Finish("capi_tests");
}
