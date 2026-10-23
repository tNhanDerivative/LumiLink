#pragma once

/* Log callback shared by the C++ API and the C API.
 * On Windows the callback uses __stdcall so that it can be implemented by
 * .NET delegates declared with CallingConvention.Winapi. */
#if defined(_WIN32)
#  define LUMILINK_CALLBACK __stdcall
#else
#  define LUMILINK_CALLBACK
#endif

/* level: 0 = info, 1 = error. message is UTF-8 and only valid during the call. */
typedef void (LUMILINK_CALLBACK *LumiLinkLogCallback)(int level, const char* message);
