#pragma once

#include <cstddef>

/// GATT profile exposed by LumiLink firmware.
/**
 * LumiLink devices use the Nordic UART Service (NUS), the de-facto "serial
 * port over BLE" supported out of the box by ESP32 and nRF SDKs:
 *  - the central (this library) writes packets to the RX characteristic,
 *  - the device sends replies (handshake) as notifications on TX.
 *
 * Every native PAL reads the UUIDs from here, so switching to a custom
 * service only requires editing this file.
 */
namespace lumilink::pal::gatt {

constexpr const char* kServiceUuid = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char* kRxCharacteristicUuid = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";  ///< central -> device (write)
constexpr const char* kTxCharacteristicUuid = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";  ///< device -> central (notify)

/// Payload size guaranteed by every BLE link (default ATT MTU 23 - 3 bytes of header).
constexpr std::size_t kDefaultWriteChunk = 20;

}  // namespace lumilink::pal::gatt
