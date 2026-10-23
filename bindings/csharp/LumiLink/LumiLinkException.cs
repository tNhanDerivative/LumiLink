using System;

namespace LumiLink;

/// <summary>Thrown when a native call returns an error code.</summary>
public sealed class LumiLinkException : Exception
{
    public LumiLinkException(LumiLinkResult result, string message)
        : base($"{message} ({result})")
    {
        Result = result;
    }

    public LumiLinkResult Result { get; }
}
