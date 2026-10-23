using System;
using System.Runtime.InteropServices;

namespace LumiLink;

/// <summary>
/// .NET-friendly entry point of the LumiLink native library.
/// Errors become <see cref="LumiLinkException"/>, the native handle is released
/// by <see cref="Dispose"/> (or by the finalizer of the underlying SafeHandle).
/// </summary>
/// <example>
/// using var leds = new LedClient();
/// leds.ChangeDeviceManager(DeviceManagerKind.Bluetooth);
/// leds.FindDevice();
/// leds.SetColor(LedClient.AllLeds, new LedColor(255, 0, 0));
/// </example>
public sealed class LedClient : IDisposable
{
    /// <summary>LED index addressing every LED of the device.</summary>
    public const byte AllLeds = 0xFF;

    // Keeps the delegate alive while native code holds a pointer to it:
    // without this reference the GC could collect it and the next log would crash.
    private static NativeMethods.LogCallback? s_nativeCallback;
    private static Action<LogLevel, string>? s_logHandler;
    private static readonly object s_logLock = new();

    private readonly LumiLinkSafeHandle _handle;

    public LedClient()
    {
        _handle = NativeMethods.ll_create();
        if (_handle.IsInvalid)
        {
            _handle.Dispose();
            throw new LumiLinkException(LumiLinkResult.InternalError, "Could not create the native LumiLink instance");
        }
    }

    public void Dispose() => _handle.Dispose();

    // ---------------------------------------------------------------- Device

    public void ChangeDeviceManager(DeviceManagerKind kind) =>
        Check(NativeMethods.ll_change_device_manager(_handle, (int)kind), "ChangeDeviceManager");

    /// <summary>Scans and connects. May block for several seconds with <see cref="DeviceManagerKind.Bluetooth"/>.</summary>
    public void FindDevice() => Check(NativeMethods.ll_find_device(_handle), "FindDevice");

    public bool HasDevice
    {
        get
        {
            int result = NativeMethods.ll_has_device(_handle);
            if (result < 0)
                Check(result, "HasDevice");
            return result == 1;
        }
    }

    /// <summary>Sends raw bytes, bypassing the packet protocol.</summary>
    public void Write(byte[] data)
    {
        ArgumentNullException.ThrowIfNull(data);
        Check(NativeMethods.ll_write(_handle, data, data.Length), "Write");
    }

    // ---------------------------------------------------------------- Single LED

    public void SetColor(byte led, LedColor color) =>
        Check(NativeMethods.ll_set_color(_handle, led, color.R, color.G, color.B), "SetColor");

    public void SetBrightness(byte led, byte brightness) =>
        Check(NativeMethods.ll_set_brightness(_handle, led, brightness), "SetBrightness");

    /// <param name="speed">0 (slowest) to 255 (fastest).</param>
    /// <param name="color">Ignored by <see cref="Effect.Rainbow"/>.</param>
    public void SetEffect(byte led, Effect effect, byte speed, LedColor color = default) =>
        Check(NativeMethods.ll_set_effect(_handle, led, (int)effect, speed, color.R, color.G, color.B), "SetEffect");

    public void TurnOff(byte led) => Check(NativeMethods.ll_turn_off(_handle, led), "TurnOff");

    // ---------------------------------------------------------------- Light groups

    public void NewGroup(string name) =>
        Check(NativeMethods.ll_new_group(_handle, NotNull(name)), "NewGroup");

    public void AddToGroup(string name, byte led) =>
        Check(NativeMethods.ll_add_to_group(_handle, NotNull(name), led), "AddToGroup");

    public void RemoveFromGroup(string name, byte led) =>
        Check(NativeMethods.ll_remove_from_group(_handle, NotNull(name), led), "RemoveFromGroup");

    public void SetGroupColor(string name, LedColor color) =>
        Check(NativeMethods.ll_set_group_color(_handle, NotNull(name), color.R, color.G, color.B), "SetGroupColor");

    public void SetGroupBrightness(string name, byte brightness) =>
        Check(NativeMethods.ll_set_group_brightness(_handle, NotNull(name), brightness), "SetGroupBrightness");

    public void SetGroupEffect(string name, Effect effect, byte speed, LedColor color = default) =>
        Check(NativeMethods.ll_set_group_effect(_handle, NotNull(name), (int)effect, speed, color.R, color.G, color.B),
              "SetGroupEffect");

    public void TurnOffGroup(string name) =>
        Check(NativeMethods.ll_turn_off_group(_handle, NotNull(name)), "TurnOffGroup");

    // ---------------------------------------------------------------- Logging (process-wide)

    public static LogMode LogMode
    {
        get => (LogMode)NativeMethods.ll_get_log_mode();
        set => Check(NativeMethods.ll_set_log_mode((int)value), "LogMode");
    }

    public static void SetLogFile(string path) =>
        Check(NativeMethods.ll_set_log_file(NotNull(path)), "SetLogFile");

    /// <summary>
    /// Routes native log messages to <paramref name="handler"/> (pass null to stop).
    /// The handler can be called from any thread.
    /// </summary>
    public static void SetLogHandler(Action<LogLevel, string>? handler)
    {
        lock (s_logLock)
        {
            s_logHandler = handler;
            if (handler is null)
            {
                NativeMethods.ll_set_log_callback(null);
                s_nativeCallback = null;
                return;
            }
            s_nativeCallback ??= OnNativeLog;
            NativeMethods.ll_set_log_callback(s_nativeCallback);
            LogMode = LogMode.Callback;
        }
    }

    public static string Describe(LumiLinkResult result) =>
        Marshal.PtrToStringUTF8(NativeMethods.ll_result_string((int)result)) ?? result.ToString();

    // ---------------------------------------------------------------- Helpers

    private static void OnNativeLog(int level, IntPtr message)
    {
        // A .NET exception must never unwind into native frames.
        try
        {
            s_logHandler?.Invoke((LogLevel)level, Marshal.PtrToStringUTF8(message) ?? string.Empty);
        }
        catch
        {
            // Swallowed on purpose: the native library cannot handle it.
        }
    }

    private static void Check(int result, string operation)
    {
        if (result != (int)LumiLinkResult.Ok)
            throw new LumiLinkException((LumiLinkResult)result, $"{operation} failed: {Describe((LumiLinkResult)result)}");
    }

    private static string NotNull(string value) =>
        value ?? throw new ArgumentNullException(nameof(value));
}
