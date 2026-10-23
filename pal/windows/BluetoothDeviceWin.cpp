// Windows PAL: Bluetooth Low Energy through the WinRT APIs (C++/WinRT).
//
// Requirements: Windows 10 1803+ (GattSession, GetGattServicesForUuidAsync),
// Windows 10 SDK >= 10.0.17763 for the C++/WinRT headers, a BLE-capable adapter.
// Works in unpackaged desktop applications; no manifest capability is needed.
//
// Threading model: the IBluetoothDevice API is blocking, so WinRT async
// operations are awaited with .get(). That is forbidden on STA threads (UI
// threads of WPF/WinForms/MAUI apps), so calls made from an STA are executed on
// a short-lived worker thread. The process-wide MTA is kept alive with
// CoIncrementMTAUsage, so such worker threads (and threads that never
// initialized COM) are implicitly part of it. Notifications arrive on WinRT
// thread-pool threads and go through the ReceiveBuffer.

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <combaseapi.h>

#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <exception>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>

#include "BluetoothError.hpp"
#include "IBluetoothDevice.hpp"
#include "common/LumiLinkGatt.hpp"
#include "common/ReceiveBuffer.hpp"

namespace lumilink::pal {

namespace {

namespace bt = winrt::Windows::Devices::Bluetooth;
namespace adv = winrt::Windows::Devices::Bluetooth::Advertisement;
namespace gattapi = winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
namespace radios = winrt::Windows::Devices::Radios;
namespace streams = winrt::Windows::Storage::Streams;

/// "6E400001-B5A3-F393-E0A9-E50E24DCCA9E" -> winrt::guid. Input comes from LumiLinkGatt.hpp.
winrt::guid ParseGuid(const std::string& text)
{
  const auto hex = [&text](std::size_t position, std::size_t length) {
    return std::stoull(text.substr(position, length), nullptr, 16);
  };
  winrt::guid result{};
  result.Data1 = static_cast<std::uint32_t>(hex(0, 8));
  result.Data2 = static_cast<std::uint16_t>(hex(9, 4));
  result.Data3 = static_cast<std::uint16_t>(hex(14, 4));
  result.Data4[0] = static_cast<std::uint8_t>(hex(19, 2));
  result.Data4[1] = static_cast<std::uint8_t>(hex(21, 2));
  for (std::size_t i = 0; i < 6; ++i)
    result.Data4[2 + i] = static_cast<std::uint8_t>(hex(24 + 2 * i, 2));
  return result;
}

std::string FormatAddress(std::uint64_t address)
{
  char text[18];
  std::snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", static_cast<unsigned>((address >> 40) & 0xFF),
                static_cast<unsigned>((address >> 32) & 0xFF), static_cast<unsigned>((address >> 24) & 0xFF),
                static_cast<unsigned>((address >> 16) & 0xFF), static_cast<unsigned>((address >> 8) & 0xFF),
                static_cast<unsigned>(address & 0xFF));
  return text;
}

std::uint64_t ParseAddress(const std::string& address)
{
  if (address.size() != 17)
    throw BluetoothError("invalid Bluetooth address: " + address);
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < address.size(); ++i) {
    const char c = address[i];
    if (i % 3 == 2) {
      if (c != ':')
        throw BluetoothError("invalid Bluetooth address: " + address);
      continue;
    }
    if (!std::isxdigit(static_cast<unsigned char>(c)))
      throw BluetoothError("invalid Bluetooth address: " + address);
    value = (value << 4) | std::stoull(std::string(1, c), nullptr, 16);
  }
  return value;
}

const char* ToString(gattapi::GattCommunicationStatus status)
{
  switch (status) {
  case gattapi::GattCommunicationStatus::Success: return "success";
  case gattapi::GattCommunicationStatus::Unreachable: return "device unreachable";
  case gattapi::GattCommunicationStatus::ProtocolError: return "protocol error";
  case gattapi::GattCommunicationStatus::AccessDenied: return "access denied";
  }
  return "unknown status";
}

/// Runs `f`, converting WinRT errors into the only exception the PAL contract allows.
template <class F>
auto Translate(F&& f) -> decltype(f())
{
  try {
    return f();
  }
  catch (const winrt::hresult_error& e) {
    throw BluetoothError(winrt::to_string(e.message()));
  }
}

bool OnStaThread()
{
  APTTYPE type{};
  APTTYPEQUALIFIER qualifier{};
  if (FAILED(CoGetApartmentType(&type, &qualifier)))
    return false;  // COM not initialized on this thread: implicit MTA
  return type == APTTYPE_STA || type == APTTYPE_MAINSTA;
}

/// Runs `f` where blocking on WinRT async operations is allowed.
template <class F>
auto RunBlocking(F&& f) -> decltype(f())
{
  if (!OnStaThread())
    return Translate(f);

  using Result = decltype(f());
  std::exception_ptr error;
  if constexpr (std::is_void_v<Result>) {
    std::thread([&] {
      try {
        Translate(f);
      }
      catch (...) {
        error = std::current_exception();
      }
    }).join();
    if (error)
      std::rethrow_exception(error);
  }
  else {
    std::optional<Result> result;
    std::thread([&] {
      try {
        result.emplace(Translate(f));
      }
      catch (...) {
        error = std::current_exception();
      }
    }).join();
    if (error)
      std::rethrow_exception(error);
    return std::move(*result);
  }
}

/// Keeps the process-wide MTA alive for the lifetime of the device object.
class MtaUsage {
public:
  MtaUsage() { winrt::check_hresult(CoIncrementMTAUsage(&cookie_)); }
  ~MtaUsage() { CoDecrementMTAUsage(cookie_); }
  MtaUsage(const MtaUsage&) = delete;
  MtaUsage& operator=(const MtaUsage&) = delete;

private:
  CO_MTA_USAGE_COOKIE cookie_{};
};

class BluetoothDeviceWin final : public IBluetoothDevice {
public:
  BluetoothDeviceWin()
      : serviceUuid_(ParseGuid(gatt::kServiceUuid)),
        rxUuid_(ParseGuid(gatt::kRxCharacteristicUuid)),
        txUuid_(ParseGuid(gatt::kTxCharacteristicUuid)),
        rx_(std::make_shared<ReceiveBuffer>()),
        linkUp_(std::make_shared<std::atomic<bool>>(false))
  {
  }

  ~BluetoothDeviceWin() override { Close(); }

  std::vector<DeviceInfo> Scan(std::chrono::milliseconds timeout) override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return RunBlocking([&] {
      EnsureAdapter();

      // Shared with the event handler, which may still run briefly after Stop().
      struct Found {
        std::mutex mutex;
        std::map<std::uint64_t, std::string> names;  // address -> advertised name
      };
      auto found = std::make_shared<Found>();

      adv::BluetoothLEAdvertisementWatcher watcher;
      watcher.ScanningMode(adv::BluetoothLEScanningMode::Active);  // scan responses carry the name
      auto received = watcher.Received(
          winrt::auto_revoke, [found](const adv::BluetoothLEAdvertisementWatcher&,
                                      const adv::BluetoothLEAdvertisementReceivedEventArgs& args) {
            const std::string name = winrt::to_string(args.Advertisement().LocalName());
            std::lock_guard<std::mutex> guard(found->mutex);
            std::string& known = found->names[args.BluetoothAddress()];
            if (!name.empty())
              known = name;
          });

      watcher.Start();
      std::this_thread::sleep_for(timeout);
      const auto status = watcher.Status();
      watcher.Stop();
      received.revoke();
      if (status == adv::BluetoothLEAdvertisementWatcherStatus::Aborted)
        throw BluetoothError("Bluetooth scan aborted: is the adapter turned on?");

      std::vector<DeviceInfo> devices;
      std::lock_guard<std::mutex> guard(found->mutex);
      for (const auto& [address, name] : found->names)
        devices.push_back(DeviceInfo{FormatAddress(address), name});
      return devices;
    });
  }

  void Connect(const std::string& address) override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    CloseLocked();
    const std::uint64_t rawAddress = ParseAddress(address);
    try {
      RunBlocking([&] { ConnectLocked(address, rawAddress); });
    }
    catch (...) {
      CloseLocked();
      throw;
    }
  }

  void Close() noexcept override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    CloseLocked();
  }

  bool IsConnected() const override { return linkUp_->load(); }

  void Write(const std::uint8_t* data, std::size_t length) override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!linkUp_->load() || !rxCharacteristic_)
      throw BluetoothError("write on a closed connection");

    RunBlocking([&] {
      const auto option =
          withResponse_ ? gattapi::GattWriteOption::WriteWithResponse : gattapi::GattWriteOption::WriteWithoutResponse;
      for (std::size_t offset = 0; offset < length; offset += writeChunk_) {
        const std::size_t size = std::min(writeChunk_, length - offset);
        streams::DataWriter writer;
        writer.WriteBytes(winrt::array_view<const std::uint8_t>(data + offset, data + offset + size));
        const auto status = rxCharacteristic_.WriteValueAsync(writer.DetachBuffer(), option).get();
        if (status != gattapi::GattCommunicationStatus::Success)
          throw BluetoothError(std::string("write failed: ") + ToString(status));
      }
    });
  }

  std::size_t Read(std::uint8_t* buffer, std::size_t length, std::chrono::milliseconds timeout) override
  {
    return rx_->Pop(buffer, length, timeout);
  }

private:
  void EnsureAdapter()
  {
    const bt::BluetoothAdapter adapter = bt::BluetoothAdapter::GetDefaultAsync().get();
    if (!adapter)
      throw BluetoothError("no Bluetooth adapter found");
    if (!adapter.IsLowEnergySupported())
      throw BluetoothError("the Bluetooth adapter does not support Bluetooth Low Energy");
    const radios::Radio radio = adapter.GetRadioAsync().get();
    if (radio && radio.State() != radios::RadioState::On)
      throw BluetoothError("Bluetooth is turned off");
  }

  gattapi::GattCharacteristic FindCharacteristic(const gattapi::GattDeviceService& service, const winrt::guid& uuid)
  {
    const auto result = service.GetCharacteristicsForUuidAsync(uuid, bt::BluetoothCacheMode::Uncached).get();
    if (result.Status() != gattapi::GattCommunicationStatus::Success || result.Characteristics().Size() == 0)
      throw BluetoothError("not a LumiLink device: RX/TX characteristics not found");
    return result.Characteristics().GetAt(0);
  }

  void ConnectLocked(const std::string& address, std::uint64_t rawAddress)
  {
    EnsureAdapter();

    device_ = bt::BluetoothLEDevice::FromBluetoothAddressAsync(rawAddress).get();
    if (!device_)
      throw BluetoothError("unknown device: " + address);

    // Windows connects lazily: the first GATT request opens the link.
    const auto services = device_.GetGattServicesForUuidAsync(serviceUuid_, bt::BluetoothCacheMode::Uncached).get();
    if (services.Status() != gattapi::GattCommunicationStatus::Success)
      throw BluetoothError("cannot reach " + address + ": " + ToString(services.Status()));
    if (services.Services().Size() == 0)
      throw BluetoothError("not a LumiLink device: LED service not found");
    service_ = services.Services().GetAt(0);
    rxCharacteristic_ = FindCharacteristic(service_, rxUuid_);
    txCharacteristic_ = FindCharacteristic(service_, txUuid_);

    // Without a maintained session Windows may drop an idle connection.
    session_ = gattapi::GattSession::FromDeviceIdAsync(device_.BluetoothDeviceId()).get();
    if (session_) {
      session_.MaintainConnection(true);
      const std::size_t pdu = session_.MaxPduSize();
      if (pdu > 3)
        writeChunk_ = std::max<std::size_t>(gatt::kDefaultWriteChunk, pdu - 3);
    }

    const auto properties = rxCharacteristic_.CharacteristicProperties();
    withResponse_ = (properties & gattapi::GattCharacteristicProperties::Write) !=
                    gattapi::GattCharacteristicProperties::None;

    // Handlers capture shared state only: they can fire on pool threads after Close().
    rx_->Open();  // before subscribing, so the first notification cannot be lost
    valueChanged_ = txCharacteristic_.ValueChanged(
        winrt::auto_revoke,
        [buffer = rx_](const gattapi::GattCharacteristic&, const gattapi::GattValueChangedEventArgs& args) {
          const streams::IBuffer value = args.CharacteristicValue();
          std::vector<std::uint8_t> bytes(value.Length());
          streams::DataReader::FromBuffer(value).ReadBytes(bytes);
          buffer->Push(bytes.data(), bytes.size());
        });
    connectionChanged_ = device_.ConnectionStatusChanged(
        winrt::auto_revoke,
        [buffer = rx_, linkUp = linkUp_](const bt::BluetoothLEDevice& device, const winrt::Windows::Foundation::IInspectable&) {
          if (device.ConnectionStatus() == bt::BluetoothConnectionStatus::Disconnected) {
            linkUp->store(false);
            buffer->Close();
          }
        });

    const auto status = txCharacteristic_
                            .WriteClientCharacteristicConfigurationDescriptorAsync(
                                gattapi::GattClientCharacteristicConfigurationDescriptorValue::Notify)
                            .get();
    if (status != gattapi::GattCommunicationStatus::Success)
      throw BluetoothError(std::string("cannot subscribe to notifications: ") + ToString(status));

    linkUp_->store(true);
  }

  void CloseLocked() noexcept
  {
    linkUp_->store(false);
    rx_->Close();
    valueChanged_ = {};
    connectionChanged_ = {};
    try {
      RunBlocking([this] {
        if (txCharacteristic_) {
          try {
            txCharacteristic_
                .WriteClientCharacteristicConfigurationDescriptorAsync(
                    gattapi::GattClientCharacteristicConfigurationDescriptorValue::None)
                .get();
          }
          catch (const winrt::hresult_error&) {
            // Already disconnected: nothing to unsubscribe from.
          }
        }
        // Releasing every reference is what makes Windows drop the BLE link.
        if (session_)
          session_.Close();
        if (service_)
          service_.Close();
        if (device_)
          device_.Close();
      });
    }
    catch (...) {
    }
    rxCharacteristic_ = nullptr;
    txCharacteristic_ = nullptr;
    session_ = nullptr;
    service_ = nullptr;
    device_ = nullptr;
    writeChunk_ = gatt::kDefaultWriteChunk;
    withResponse_ = true;
  }

  MtaUsage mta_;  // first member: alive for as long as any WinRT object below
  const winrt::guid serviceUuid_;
  const winrt::guid rxUuid_;
  const winrt::guid txUuid_;

  std::mutex mutex_;
  std::shared_ptr<ReceiveBuffer> rx_;
  std::shared_ptr<std::atomic<bool>> linkUp_;

  bt::BluetoothLEDevice device_{nullptr};
  gattapi::GattSession session_{nullptr};
  gattapi::GattDeviceService service_{nullptr};
  gattapi::GattCharacteristic rxCharacteristic_{nullptr};
  gattapi::GattCharacteristic txCharacteristic_{nullptr};
  gattapi::GattCharacteristic::ValueChanged_revoker valueChanged_;
  bt::BluetoothLEDevice::ConnectionStatusChanged_revoker connectionChanged_;

  std::size_t writeChunk_ = gatt::kDefaultWriteChunk;
  bool withResponse_ = true;
};

}  // namespace

std::unique_ptr<IBluetoothDevice> CreateBluetoothDevice()
{
  return std::make_unique<BluetoothDeviceWin>();
}

}  // namespace lumilink::pal
