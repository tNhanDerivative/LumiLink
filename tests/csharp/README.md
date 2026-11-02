# Test binding C# (.NET)

Console app tự kiểm tra binding `bindings/csharp/LumiLink` (P/Invoke, `SafeHandle`, log callback, mã lỗi → exception). Không dùng framework test: mỗi check in `[PASS]` hoặc `[FAIL]`, kết thúc bằng `ALL PASS` (exit code 0) hoặc `N CHECK(S) FAILED` (exit code 1).

Mặc định app dùng PAL **mock** nên chạy được mà không cần đèn hay Bluetooth.

## 1. Yêu cầu

| Cần | Ghi chú |
|---|---|
| .NET SDK 8 trở lên | Kiểm tra: `dotnet --version`. Cài trên macOS: `brew install --cask dotnet-sdk` |
| Shared library đã build | Mặc định lấy từ `build/bin` (xem mục 2) |

App nhắm `net10.0` (bản đã thử), còn binding nhắm `net8.0`. Nếu máy chỉ có runtime 8, đổi `TargetFramework` trong `LumiLink.Smoke.csproj` thành `net8.0`, hoặc đặt `DOTNET_ROLL_FORWARD=Major`.

Kiến trúc CPU của .NET phải khớp với shared library (ví dụ cả hai đều arm64 trên Apple Silicon). Kiểm tra thư viện: `lipo -info build/bin/liblumilink.dylib`.

## 2. Build thư viện native

Chỉ cần làm một lần, hoặc khi đổi code C/C++:

```bash
cmake -S . -B build
cmake --build build -j
```

Lệnh này tạo `build/bin/liblumilink.dylib` (macOS), `liblumilink.so` (Linux) hoặc `lumilink.dll` (Windows).

## 3. Chạy test

Từ thư mục gốc của repo:

```bash
dotnet run --project tests/csharp
```

Kết quả mong đợi (rút gọn):

```text
== 2. Smoke (Text manager, khong can den) ==
[PASS] chuoi lenh Text chay het, khong exception
...
== 6. Raw vs golden raw_sequence.bin ==
[PASS] tung byte khop golden

ALL PASS
```

Chỉ build, không chạy: `dotnet build tests/csharp`. Project này tự build luôn `bindings/csharp/LumiLink` qua `ProjectReference`.

## 4. Test kiểm tra những gì

Số thứ tự khớp với bảng kiểm chứng ở `doc_lumilink/03-build-test-kiem-chung.md` (mục 7).

| Nhóm | Kiểm tra | Phát hiện lỗi gì |
|---|---|---|
| 2. Smoke | Chuỗi lệnh với `DeviceManagerKind.Text` chạy hết; log callback nhận được log | Không load được thư viện, sai tên hàm export, sai calling convention |
| 3. Đường lỗi | `NoSuchGroup`, `GroupExists`, `LedAlreadyInGroup`, `LedNotInGroup`; manager ngoài phạm vi → `InvalidArgument`; `null` → `ArgumentNullException`; `Dispose` hai lần; dùng sau `Dispose` → `ObjectDisposedException`; `Bluetooth` không crash | Sai ánh xạ mã lỗi → exception, lỗi quản lý handle |
| 4. Log callback | Exception trong handler không lan vào native; `SetLogHandler(null)` dừng log; đăng ký lại sau GC vẫn nhận log | Delegate bị GC thu hồi, exception unwind vào native frame |
| 5. Stress | 10.000 client create/`Dispose`; 5.000 client bỏ cho finalizer; 8 thread × 500 lệnh trên một client | Rò handle, crash trong finalizer, data race |
| 6. Raw so với golden | Chạy kịch bản `RunRawScenario()` trong process con, so từng byte stdout với `tests/golden/raw_sequence.bin` | Tham số truyền xuống native sai thứ tự hoặc sai kiểu |

Lưu ý nhóm 3: với PAL mock, `Bluetooth` + `FindDevice()` trả `Ok` và `HasDevice` là `True`.

## 5. Test với Bluetooth thật

Cần đèn LumiLink (firmware NUS) đang bật ở gần.

```bash
cmake -S . -B build-native -DLUMILINK_PAL=native -DCMAKE_BUILD_TYPE=Release
cmake --build build-native -j
./build-native/bin/pal_tests        # phải pass trước, nếu native không nói chuyện được với đèn thì C# cũng không
dotnet run --project tests/csharp -p:LumiLinkNativeDir=$(pwd)/build-native/bin
```

Để bật đèn thật, thêm đoạn thử riêng vào cuối nhóm 3 hoặc một app khác, ví dụ:

```csharp
using var c = new LedClient();
c.ChangeDeviceManager(DeviceManagerKind.Bluetooth);
c.FindDevice();                                             // có thể block vài giây
c.SetColor(LedClient.AllLeds, new LedColor(255, 0, 0));    // đèn phải sáng đỏ
```

Trên macOS, CoreBluetooth cần quyền Bluetooth. Nếu process bị chặn hoặc crash ngay khi quét, cấp quyền cho ứng dụng Terminal trong System Settings → Privacy & Security → Bluetooth.

## 6. Khi test fail

| Triệu chứng | Nguyên nhân thường gặp | Cách xử lý |
|---|---|---|
| `DllNotFoundException: lumilink` | Chưa build native, hoặc sai `LumiLinkNativeDir`, hoặc sai kiến trúc CPU | Làm mục 2; kiểm tra `ls tests/csharp/bin/Debug/net10.0/` có `liblumilink.dylib` |
| `EntryPointNotFoundException` | Thư viện native cũ, thiếu hàm `ll_*` | Build lại native |
| `You must install or update .NET to run this application` | Chỉ có runtime khác phiên bản | Xem mục 1 |
| Nhóm 6 fail, in `expected:` và `actual:` | Protocol đổi, hoặc `RunRawScenario()` lệch `RunScenario()` | Xem mục 7 |
| Crash không có exception | Lỗi ở ranh giới native, thường là calling convention hoặc vòng đời delegate | Chạy lại nhóm 4 riêng, thu hẹp bằng cách comment các nhóm khác |
| Build lỗi `MSB3030` về file `.so` | Không có file khớp trong `LumiLinkNativeDir` | Item dùng wildcard nên chỉ lỗi nếu bạn sửa thành đường dẫn cố định |

## 7. Giữ test đồng bộ với test C++

- `RunRawScenario()` trong `Program.cs` phải luôn giống `RunScenario()` trong `tests/core/GoldenRawTests.cpp`. Đổi một bên thì đổi bên kia.
- Khi cố ý đổi protocol, sinh lại golden bằng `LUMILINK_UPDATE_GOLDEN=1 ./build/bin/golden_raw_tests` (xem mục 6.3 của `doc_lumilink/03-build-test-kiem-chung.md`), kiểm tra bằng `xxd tests/golden/raw_sequence.bin`, rồi chạy lại test C#.
- Thêm hàm vào C API: thêm khai báo vào `NativeMethods.cs`, bọc trong `LedClient.cs`, rồi thêm một check vào nhóm tương ứng trong `Program.cs`.

## 8. Cấu trúc thư mục

```text
tests/csharp/
├── LumiLink.Smoke.csproj   # tham chiếu bindings/csharp/LumiLink, copy thư viện native và golden vào thư mục output
├── Program.cs              # các nhóm test 2 đến 6, và chế độ con `raw` cho nhóm 6
└── README.md
```

`Program.cs` chạy lại chính nó với tham số `raw` ở process con để bắt dữ liệu native ghi ra stdout. Vì vậy đừng in thêm gì ra stdout trong nhánh `raw`.
