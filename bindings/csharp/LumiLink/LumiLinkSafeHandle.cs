using Microsoft.Win32.SafeHandles;

namespace LumiLink;

/// <summary>
/// Owns a LumiLinkHandle* allocated by the native library.
/// The memory is allocated and released by the library (ll_create / ll_destroy):
/// .NET only decides *when* to give it back, through Dispose or finalization.
/// </summary>
internal sealed class LumiLinkSafeHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    // Called by the marshaller when ll_create returns.
    private LumiLinkSafeHandle() : base(ownsHandle: true)
    {
    }

    protected override bool ReleaseHandle()
    {
        NativeMethods.ll_destroy(handle);
        return true;
    }
}
