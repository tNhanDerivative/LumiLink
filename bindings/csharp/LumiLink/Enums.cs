namespace LumiLink;

/// <summary>Result codes returned by the native library (LL_* in LumiLinkCAPI.h).</summary>
public enum LumiLinkResult
{
    Ok = 0,
    NotConnected = 1,
    WriteFailed = 2,
    NoSuchGroup = 3,
    GroupExists = 4,
    LedAlreadyInGroup = 5,
    LedNotInGroup = 6,
    NoSuchManager = 7,
    DeviceNotFound = 8,
    ConnectFailed = 9,
    HandshakeFailed = 10,
    ScanFailed = 11,

    InvalidHandle = -1,
    InvalidArgument = -2,
    InternalError = -99,
}

/// <summary>How orders reach the device.</summary>
public enum DeviceManagerKind
{
    /// <summary>Logs orders in a readable form, no device needed.</summary>
    Text = 0,
    /// <summary>Writes raw packets to the process standard output.</summary>
    Raw = 1,
    /// <summary>Sends orders to a LumiLink device over Bluetooth.</summary>
    Bluetooth = 2,
}

/// <summary>Effects run by the device firmware.</summary>
public enum Effect
{
    None = 0,
    Blink = 1,
    Breathe = 2,
    Rainbow = 3,
}

public enum LogMode
{
    StdOut = 0,
    File = 1,
    Callback = 2,
    Silent = 3,
}

public enum LogLevel
{
    Info = 0,
    Error = 1,
}

/// <summary>RGB color, 0-255 per channel.</summary>
public readonly record struct LedColor(byte R, byte G, byte B)
{
    public static readonly LedColor Black = new(0, 0, 0);
    public static readonly LedColor White = new(255, 255, 255);
}
