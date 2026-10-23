using System;
using System.Runtime.InteropServices;

namespace LumiLink;

/// <summary>
/// 1:1 import of the C API declared in LumiLinkCAPI.h. No logic here: error
/// checking and lifetime management live in <see cref="LedClient"/>.
/// </summary>
internal static class NativeMethods
{
    // Resolved as lumilink.dll / liblumilink.so / liblumilink.dylib.
    private const string Library = "lumilink";

    /// <summary>Matches LumiLinkLogCallback: __stdcall on Windows, cdecl elsewhere.</summary>
    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    internal delegate void LogCallback(int level, IntPtr message);

    // Lifetime
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern LumiLinkSafeHandle ll_create();

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void ll_destroy(IntPtr handle);

    // Device
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ll_change_device_manager(LumiLinkSafeHandle handle, int manager);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ll_find_device(LumiLinkSafeHandle handle);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ll_has_device(LumiLinkSafeHandle handle);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ll_status(LumiLinkSafeHandle handle);

    // The byte[] is pinned for the duration of the call only; the native side copies it.
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ll_write(LumiLinkSafeHandle handle, byte[] data, int length);

    // Single LED
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ll_set_color(LumiLinkSafeHandle handle, byte led, byte r, byte g, byte b);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ll_set_brightness(LumiLinkSafeHandle handle, byte led, byte brightness);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ll_set_effect(LumiLinkSafeHandle handle, byte led, int effect, byte speed,
                                             byte r, byte g, byte b);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ll_turn_off(LumiLinkSafeHandle handle, byte led);

    // Light groups (names are marshalled as UTF-8, as the C API expects)
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ll_new_group(LumiLinkSafeHandle handle,
                                            [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ll_add_to_group(LumiLinkSafeHandle handle,
                                               [MarshalAs(UnmanagedType.LPUTF8Str)] string name, byte led);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ll_remove_from_group(LumiLinkSafeHandle handle,
                                                    [MarshalAs(UnmanagedType.LPUTF8Str)] string name, byte led);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ll_set_group_color(LumiLinkSafeHandle handle,
                                                  [MarshalAs(UnmanagedType.LPUTF8Str)] string name,
                                                  byte r, byte g, byte b);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ll_set_group_brightness(LumiLinkSafeHandle handle,
                                                       [MarshalAs(UnmanagedType.LPUTF8Str)] string name,
                                                       byte brightness);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ll_set_group_effect(LumiLinkSafeHandle handle,
                                                   [MarshalAs(UnmanagedType.LPUTF8Str)] string name,
                                                   int effect, byte speed, byte r, byte g, byte b);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ll_turn_off_group(LumiLinkSafeHandle handle,
                                                 [MarshalAs(UnmanagedType.LPUTF8Str)] string name);

    // Logging
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ll_set_log_mode(int mode);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ll_get_log_mode();

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int ll_set_log_file([MarshalAs(UnmanagedType.LPUTF8Str)] string path);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern void ll_set_log_callback(LogCallback? callback);

    // Returns a static native string: never freed by .NET.
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    internal static extern IntPtr ll_result_string(int result);
}
