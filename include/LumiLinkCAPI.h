#pragma once

/* Public C API of LumiLink.
 *
 * This is the only interface exported by the shared library (lumilink.dll,
 * liblumilink.so, liblumilink.dylib). Every function is a 1:1 wrapper around
 * lumilink::LumiLink (see LumiLink/LumiLink.hpp) and never lets a C++
 * exception cross the library boundary.
 *
 * Conventions:
 *  - Functions return an int result code (LL_OK on success).
 *  - Strings are UTF-8, owned by the caller and only read during the call.
 *  - A LumiLinkHandle is created by ll_create() and must be released with
 *    ll_destroy(). The library owns the memory behind the handle.
 */

#include <stdint.h>

#include "LumiLinkLogCallback.h"

#if defined(_WIN32)
#  if defined(LUMILINK_BUILDING)
#    define LUMILINK_API __declspec(dllexport)
#  else
#    define LUMILINK_API __declspec(dllimport)
#  endif
#else
#  define LUMILINK_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Result codes. Positive values mirror lumilink::Result. */
enum {
  LL_OK = 0,
  LL_ERR_NOT_CONNECTED = 1,
  LL_ERR_WRITE_FAILED = 2,
  LL_ERR_NO_SUCH_GROUP = 3,
  LL_ERR_GROUP_EXISTS = 4,
  LL_ERR_LED_ALREADY_IN_GROUP = 5,
  LL_ERR_LED_NOT_IN_GROUP = 6,
  LL_ERR_NO_SUCH_MANAGER = 7,
  LL_ERR_DEVICE_NOT_FOUND = 8,
  LL_ERR_CONNECT_FAILED = 9,
  LL_ERR_HANDSHAKE_FAILED = 10,
  LL_ERR_SCAN_FAILED = 11,

  /* Errors detected at the C boundary */
  LL_ERR_INVALID_HANDLE = -1,
  LL_ERR_INVALID_ARG = -2,
  LL_ERR_EXCEPTION = -99
};

/* Device managers for ll_change_device_manager() */
enum {
  LL_MANAGER_TEXT = 0, /* prints orders, no device */
  LL_MANAGER_RAW = 1,  /* writes raw packets to stdout */
  LL_MANAGER_BT = 2    /* Bluetooth device */
};

/* Effects for ll_set_effect() / ll_set_group_effect() */
enum {
  LL_EFFECT_NONE = 0,
  LL_EFFECT_BLINK = 1,
  LL_EFFECT_BREATHE = 2,
  LL_EFFECT_RAINBOW = 3
};

/* Log modes for ll_set_log_mode() */
enum {
  LL_LOG_STDOUT = 0,
  LL_LOG_FILE = 1,
  LL_LOG_CALLBACK = 2,
  LL_LOG_SILENT = 3
};

/* LED index addressing every LED */
#define LL_ALL_LEDS 0xFF

typedef struct LumiLinkHandle LumiLinkHandle;

/* Lifetime */
LUMILINK_API LumiLinkHandle* ll_create(void); /* NULL on failure */
LUMILINK_API void ll_destroy(LumiLinkHandle* handle); /* NULL is ignored */

/* Device */
LUMILINK_API int ll_change_device_manager(LumiLinkHandle* handle, int manager);
LUMILINK_API int ll_find_device(LumiLinkHandle* handle);
LUMILINK_API int ll_has_device(LumiLinkHandle* handle); /* 1 / 0, or a negative error */
LUMILINK_API int ll_status(LumiLinkHandle* handle);
LUMILINK_API int ll_write(LumiLinkHandle* handle, const uint8_t* data, int length);

/* Single LED (or LL_ALL_LEDS) */
LUMILINK_API int ll_set_color(LumiLinkHandle* handle, uint8_t led, uint8_t r, uint8_t g, uint8_t b);
LUMILINK_API int ll_set_brightness(LumiLinkHandle* handle, uint8_t led, uint8_t brightness);
LUMILINK_API int ll_set_effect(LumiLinkHandle* handle, uint8_t led, int effect, uint8_t speed,
                               uint8_t r, uint8_t g, uint8_t b);
LUMILINK_API int ll_turn_off(LumiLinkHandle* handle, uint8_t led);

/* Light groups */
LUMILINK_API int ll_new_group(LumiLinkHandle* handle, const char* name);
LUMILINK_API int ll_add_to_group(LumiLinkHandle* handle, const char* name, uint8_t led);
LUMILINK_API int ll_remove_from_group(LumiLinkHandle* handle, const char* name, uint8_t led);
LUMILINK_API int ll_set_group_color(LumiLinkHandle* handle, const char* name, uint8_t r, uint8_t g, uint8_t b);
LUMILINK_API int ll_set_group_brightness(LumiLinkHandle* handle, const char* name, uint8_t brightness);
LUMILINK_API int ll_set_group_effect(LumiLinkHandle* handle, const char* name, int effect, uint8_t speed,
                                     uint8_t r, uint8_t g, uint8_t b);
LUMILINK_API int ll_turn_off_group(LumiLinkHandle* handle, const char* name);

/* Logging (global, usable before ll_create) */
LUMILINK_API int ll_set_log_mode(int mode);
LUMILINK_API int ll_get_log_mode(void);
LUMILINK_API int ll_set_log_file(const char* path);
LUMILINK_API void ll_set_log_callback(LumiLinkLogCallback callback); /* NULL unregisters */

/* Static string describing a result code. Never NULL, never freed by the caller. */
LUMILINK_API const char* ll_result_string(int result);

#ifdef __cplusplus
}
#endif
