# LumiLink

Thư viện C++17 đa nền tảng để điều khiển đèn LED qua Bluetooth: màu RGB, độ sáng, hiệu ứng (blink / breathe / rainbow) và nhóm LED. Core viết một lần; ra bên ngoài có C++ API, C API (shared library) và binding C#/.NET.

> Trạng thái: có PAL Bluetooth Low Energy native cho macOS (CoreBluetooth), Linux (BlueZ) và Windows (WinRT), cùng một PAL **mock** (thiết bị giả lập) dùng mặc định để phát triển và test. Xem mục [PAL Bluetooth native](#pal-bluetooth-native).

## Kiến trúc

```
 Client C#/.NET ─► LedClient (5.6) ─► NativeMethods (5.5) ─┐
 Client C / ngôn ngữ khác ────────────────────────────────►├─► C API + platform boundary ─► C++ API ─► Core ─► PAL ─► Bluetooth OS
 Client C++ / iOS ──────────────────────────────────────────────────────────────────────────►┘
```

| Layer | Thư mục | Ghi chú |
|---|---|---|
| C++ Core | `core/` | Business logic: protocol, device manager, nhóm LED, log. Không biết gì về platform |
| Public C++ API | `include/LumiLink/LumiLink.hpp` | Class `lumilink::LumiLink`, pImpl. C++ client link static `lumilink_core` |
| Public C API + platform boundary | `include/LumiLinkCAPI.h`, `capi/` | `extern "C"`, handle, mã lỗi. Bắt mọi exception. Chỉ có các hàm `ll_*` được export |
| Import native (.NET) | `bindings/csharp/LumiLink/NativeMethods.cs` | `DllImport` 1:1 với C API |
| Wrapper .NET | `bindings/csharp/LumiLink/LedClient.cs` | `IDisposable`, exception, `SafeHandle`, log callback |
| PAL | `pal/` | `IBluetoothDevice` + một thư mục cho mỗi implementation; CMake chọn file nào được build |

**Quy tắc:** mọi business logic nằm trong `core/`. `capi/` và `bindings/` chỉ chuyển đổi kiểu dữ liệu và mã lỗi, vì một số client (C++, iOS) dùng thẳng C++ API và bỏ qua các layer đó.

## Build

Cần CMake ≥ 3.20 và compiler C++17 (Apple clang, GCC, MSVC).

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Output nằm trong `build/bin/`: `liblumilink.dylib` / `liblumilink.so` / `lumilink.dll`, `interactive`, và các test.

| Option | Mặc định | Ý nghĩa |
|---|---|---|
| `LUMILINK_PAL` | `mock` | `mock` hoặc `native` (PAL của OS đang build) |
| `LUMILINK_TESTS` | `ON` | Build các test executable |
| `LUMILINK_VERBOSE` | `ON` | Ghi log mức info. Log lỗi luôn được ghi |

## Sử dụng

Luồng chung: tạo instance → (tùy chọn) đổi device manager → `FindDevice` → gửi lệnh → giải phóng.

### C++

```cpp
#include "LumiLink/LumiLink.hpp"
using namespace lumilink;

LumiLink leds;                                   // mặc định: TextManager (chỉ in lệnh)
leds.ChangeDeviceManager("BTManager");
if (leds.FindDevice() != Result::Ok) return 1;

leds.SetColor(0, Color{255, 0, 0});
leds.SetBrightness(kAllLeds, 128);
leds.SetEffect(kAllLeds, Effect::Breathe, 20, Color{0, 0, 255});

leds.NewGroup("ring");
leds.AddToGroup("ring", 1);
leds.AddToGroup("ring", 2);
leds.SetGroupColor("ring", Color{0, 255, 0});
leds.TurnOffGroup("ring");
```

Link với target CMake `lumilink_core`.

### C

```c
#include "LumiLinkCAPI.h"

LumiLinkHandle* h = ll_create();
ll_change_device_manager(h, LL_MANAGER_BT);
if (ll_find_device(h) != LL_OK) { /* ll_result_string(...) */ }
ll_set_color(h, 0, 255, 0, 0);
ll_set_effect(h, LL_ALL_LEDS, LL_EFFECT_RAINBOW, 200, 0, 0, 0);
ll_destroy(h);
```

- Mọi hàm trả `int`: `LL_OK` (0), mã dương = lỗi nghiệp vụ, mã âm = lỗi ở biên (`LL_ERR_INVALID_HANDLE`, `LL_ERR_INVALID_ARG`, `LL_ERR_EXCEPTION`).
- **Ownership:** thư viện cấp phát handle trong `ll_create` và chỉ thư viện giải phóng nó trong `ll_destroy`. Chuỗi và buffer do caller sở hữu, thư viện chỉ đọc trong lúc gọi hàm.
- Nhiều handle hoạt động độc lập; không có state toàn cục ngoài cấu hình log.

### C# / .NET

```csharp
using LumiLink;

LedClient.SetLogHandler((level, msg) => Console.WriteLine($"[{level}] {msg}"));

using var leds = new LedClient();
leds.ChangeDeviceManager(DeviceManagerKind.Bluetooth);
leds.FindDevice();                                  // ném LumiLinkException nếu lỗi
leds.SetColor(LedClient.AllLeds, new LedColor(255, 120, 0));
leds.SetEffect(3, Effect.Blink, 50, LedColor.White);
```

```bash
dotnet build bindings/csharp/LumiLink -p:LumiLinkNativeDir=$(pwd)/build/bin
```

Test binding (không cần đèn với PAL mock): `dotnet run --project tests/csharp`.

`LumiLinkNativeDir` copy native library vào thư mục output. Lưu ý:
- `LumiLinkSafeHandle` gọi `ll_destroy` khi `Dispose` hoặc khi GC finalize, nên ownership vẫn thuộc về thư viện native.
- Delegate log được giữ trong static field để GC không thu hồi khi native code còn giữ con trỏ tới nó; exception trong handler bị chặn lại, không để lan vào native code.
- Binding chưa được build/test trên máy phát triển hiện tại (chưa cài .NET SDK).

## Device managers

| Tên | `LL_MANAGER_*` | Hành vi |
|---|---|---|
| `TextManager` | `TEXT` (0) | Mặc định. In lệnh dạng dễ đọc + packet hex, không cần thiết bị |
| `RawManager` | `RAW` (1) | Ghi packet nhị phân ra stdout (pipe tới thiết bị theo cách khác, hoặc golden test) |
| `BTManager` | `BT` (2) | Scan, kết nối thiết bị đầu tiên có tên bắt đầu bằng `LumiLink`, handshake, gửi lệnh qua PAL |

Nhóm LED được giữ nguyên khi đổi device manager.

## Protocol v1

| Packet | Bytes |
|---|---|
| Handshake | `'H' min max` → thiết bị trả `'H' api` |
| Set color | `'L' 1 5  'C' led r g b` |
| Set brightness | `'L' 1 3  'B' led value` |
| Set effect | `'L' 1 7  'E' led effect speed r g b` (effect: 0 none, 1 blink, 2 breathe, 3 rainbow) |
| Turn off | `'L' 1 2  'O' led` |

Header lệnh: `'L'`, version, độ dài data. `led = 0xFF` = tất cả LED. Hiệu ứng do firmware tự chạy, thư viện chỉ gửi lệnh.

## Logging

`SetLogMode` / `ll_set_log_mode`: `StdOut` (0, mặc định), `File` (1, mặc định `./LumiLinkLog.txt`), `Callback` (2), `Silent` (3). Cấu hình log là toàn cục và gọi được trước khi tạo instance. Callback nhận `(int level, const char* message)`, `level` 0 = info, 1 = error; trên Windows callback dùng `__stdcall`.

## Interactive CLI

```bash
LUMILINK_MOCK_ECHO=1 ./build/bin/interactive
> manager bt
> find
> color 0 255 0 0
> effect all breathe 10 0 0 255
> group new ring
> group add ring 1
> group color ring 0 255 0
```

`LUMILINK_MOCK_ECHO=1` cho thiết bị mock in ra mọi packet nó nhận. Gõ `help` để xem toàn bộ lệnh.

## Test

| Test | Kiểm tra |
|---|---|
| `pal_tests` | Contract của `IBluetoothDevice`, chỉ link PAL. Cùng một bộ test chạy với mọi implementation |
| `packet_tests` | Bytes của từng loại packet, handshake |
| `group_tests` | Nhóm LED: mã lỗi, gửi lệnh cho từng LED theo thứ tự |
| `btmanager_tests` | BTManager với mock: kết nối, chọn thiết bị, scan/connect/handshake/write lỗi (chỉ build với PAL mock) |
| `thread_tests` | 8 thread gửi lệnh và đổi device manager đồng thời |
| `golden_raw_tests` | Golden master: luồng byte của RawManager so với `tests/golden/raw_sequence.bin` |
| `capi_tests` | C API nhìn từ client bên ngoài: handle NULL, tham số sai, callback log, mã lỗi |

Khi cố ý đổi protocol, sinh lại golden master:

```bash
LUMILINK_UPDATE_GOLDEN=1 ./build/bin/golden_raw_tests
```

## PAL Bluetooth native

Build với `-DLUMILINK_PAL=native` để dùng Bluetooth thật:

```bash
cmake -S . -B build-native -DLUMILINK_PAL=native
cmake --build build-native
```

### Firmware: GATT profile

Thiết bị LumiLink dùng **Nordic UART Service (NUS)**, chuẩn "serial port qua BLE" mà ESP32 / nRF đều có sẵn thư viện:

| Vai trò | UUID | Hướng |
|---|---|---|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` | |
| RX characteristic | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` | thư viện → thiết bị (write) |
| TX characteristic | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` | thiết bị → thư viện (notify, dùng cho reply handshake) |

Firmware phải quảng bá tên bắt đầu bằng `LumiLink` (ví dụ `LumiLink-Desk`). Để dùng một service khác, chỉ cần sửa `pal/common/LumiLinkGatt.hpp`.

### Theo từng hệ điều hành

| OS | File | Yêu cầu | Địa chỉ thiết bị |
|---|---|---|---|
| macOS 10.15+ | `pal/macos/BluetoothDeviceMac.mm` | App phải có `NSBluetoothAlwaysUsageDescription` trong Info.plist. Tool CLI của project đã nhúng sẵn (`pal/macos/Info.plist`) | UUID của `CBPeripheral` (macOS không cho biết địa chỉ MAC) |
| Linux | `pal/linux/BluetoothDeviceLinux.cpp` | BlueZ ≥ 5.48 (`bluetoothd` đang chạy), `libsystemd-dev` ≥ 237, user có quyền dùng `org.bluez` (thường là nhóm `bluetooth`) | MAC `AA:BB:CC:DD:EE:FF` |
| Windows 10 1803+ | `pal/windows/BluetoothDeviceWin.cpp` | Windows 10 SDK ≥ 10.0.17763 (C++/WinRT), MSVC. Không cần manifest | MAC `AA:BB:CC:DD:EE:FF` |

Ghi chú:
- **macOS:** lần đầu dùng, macOS hỏi quyền Bluetooth cho *ứng dụng chịu trách nhiệm* (responsible process). Chạy `interactive` từ Terminal thì quyền được cấp cho Terminal. Nếu Info.plist thiếu usage description, PAL ném `BluetoothError` thay vì để macOS kill tiến trình.
- **Linux:** thiết bị phải được scan trước khi connect (BlueZ chỉ biết những thiết bị nó đã thấy).
- **Windows:** PAL có thể được gọi từ luồng UI (STA). Khi đó các lời gọi WinRT blocking được tự động chuyển sang một luồng MTA.

### Chạy thử với đèn thật

```bash
./build-native/bin/interactive
> manager bt
> find
> color all 255 0 0
```

`pal_tests` là contract test của PAL. Với `LUMILINK_PAL=native`, test này cần một thiết bị LumiLink đang bật ở gần và phải chạy **trên đúng platform đó**. `thread_tests` và `btmanager_tests` chỉ được build với PAL mock.

### Thêm platform mới

1. Implement `lumilink::pal::IBluetoothDevice` và `CreateBluetoothDevice()` trong `pal/<platform>/`. Có thể dùng lại `pal/common/ReceiveBuffer.hpp` (hàng đợi byte có timeout giữa luồng notify và `Read()`) và `pal/common/LumiLinkGatt.hpp`.
2. Tôn trọng contract trong `pal/IBluetoothDevice.hpp`: lỗi thì ném `pal::BluetoothError`, `Close()` không ném exception, `Read()` trả 0 khi hết timeout.
3. Thêm một nhánh cho platform đó trong khối `LUMILINK_PAL STREQUAL "native"` của `CMakeLists.txt`.

Core, C API và binding không phải sửa gì.
