#include "IBluetoothDevice.hpp"

namespace lumilink::pal {

// Out-of-line so the vtable is emitted in exactly one translation unit.
IBluetoothDevice::~IBluetoothDevice() = default;

}  // namespace lumilink::pal
