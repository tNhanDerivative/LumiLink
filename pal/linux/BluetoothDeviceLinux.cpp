// Linux PAL: Bluetooth Low Energy through BlueZ, over the D-Bus system bus (sd-bus).
//
// Threading model: sd-bus is not thread-safe, so the bus is only touched while
// holding mutex_. There is no background thread: incoming signals (GATT
// notifications, disconnections) are dispatched by Pump(), which Read(),
// Write() and IsConnected() call before looking at the connection state.
//
// Requirements: BlueZ >= 5.48 running (bluetoothd), libsystemd >= 237, and a
// user allowed to talk to org.bluez (members of the "bluetooth" group on most
// distributions).

#include <systemd/sd-bus.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

#include "BluetoothError.hpp"
#include "IBluetoothDevice.hpp"
#include "common/LumiLinkGatt.hpp"
#include "common/ReceiveBuffer.hpp"

using namespace std::chrono_literals;

namespace lumilink::pal {

namespace {

constexpr const char* kBluez = "org.bluez";
constexpr const char* kAdapterInterface = "org.bluez.Adapter1";
constexpr const char* kDeviceInterface = "org.bluez.Device1";
constexpr const char* kCharacteristicInterface = "org.bluez.GattCharacteristic1";
constexpr const char* kPropertiesInterface = "org.freedesktop.DBus.Properties";
constexpr const char* kObjectManagerInterface = "org.freedesktop.DBus.ObjectManager";

constexpr auto kCallTimeout = 5s;
constexpr auto kConnectTimeout = 20s;
constexpr auto kServicesTimeout = 10s;

// ---------------------------------------------------------------------------
// RAII wrappers around sd-bus handles
// ---------------------------------------------------------------------------

struct BusDeleter {
  void operator()(sd_bus* bus) const { sd_bus_flush_close_unref(bus); }
};
struct MessageDeleter {
  void operator()(sd_bus_message* message) const { sd_bus_message_unref(message); }
};
struct SlotDeleter {
  void operator()(sd_bus_slot* slot) const { sd_bus_slot_unref(slot); }
};
using BusPtr = std::unique_ptr<sd_bus, BusDeleter>;
using MessagePtr = std::unique_ptr<sd_bus_message, MessageDeleter>;
using SlotPtr = std::unique_ptr<sd_bus_slot, SlotDeleter>;

class BusError {
public:
  BusError() = default;
  ~BusError() { sd_bus_error_free(&error_); }
  BusError(const BusError&) = delete;
  BusError& operator=(const BusError&) = delete;

  sd_bus_error* get() { return &error_; }
  bool Is(const char* name) const { return sd_bus_error_has_name(&error_, name) != 0; }

  std::string Describe(int code) const
  {
    if (error_.message != nullptr)
      return error_.message;
    if (error_.name != nullptr)
      return error_.name;
    return std::strerror(-code);
  }

private:
  sd_bus_error error_ = SD_BUS_ERROR_NULL;
};

void Check(int result, const char* what)
{
  if (result < 0)
    throw BluetoothError(std::string(what) + ": " + std::strerror(-result));
}

std::uint64_t ToMicroseconds(std::chrono::milliseconds duration)
{
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(duration).count());
}

bool EqualsIgnoreCase(const std::string& a, const std::string& b)
{
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
         });
}

bool IsValidAddress(const std::string& address)
{
  if (address.size() != 17)
    return false;
  for (std::size_t i = 0; i < address.size(); ++i) {
    const bool separator = i % 3 == 2;
    if (separator ? address[i] != ':' : !std::isxdigit(static_cast<unsigned char>(address[i])))
      return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// D-Bus property parsing: only the types BlueZ uses for what we need
// ---------------------------------------------------------------------------

struct Properties {
  std::map<std::string, std::string> strings;
  std::map<std::string, bool> bools;
  std::map<std::string, std::int64_t> numbers;

  bool Has(const std::string& key) const
  {
    return strings.count(key) != 0 || bools.count(key) != 0 || numbers.count(key) != 0;
  }
  std::string String(const std::string& key) const
  {
    const auto it = strings.find(key);
    return it == strings.end() ? std::string() : it->second;
  }
  bool Bool(const std::string& key) const
  {
    const auto it = bools.find(key);
    return it != bools.end() && it->second;
  }
};

using Interfaces = std::map<std::string, Properties>;  // interface name -> properties
using ManagedObjects = std::map<std::string, Interfaces>;  // object path -> interfaces

/// Reads an a{sv} dictionary. Values of other types are skipped.
void ReadProperties(sd_bus_message* message, Properties& properties)
{
  Check(sd_bus_message_enter_container(message, SD_BUS_TYPE_ARRAY, "{sv}"), "parse properties");
  int result;
  while ((result = sd_bus_message_enter_container(message, SD_BUS_TYPE_DICT_ENTRY, "sv")) > 0) {
    const char* key = nullptr;
    Check(sd_bus_message_read(message, "s", &key), "parse property name");
    const std::string name = key;

    const char* contents = nullptr;
    Check(sd_bus_message_peek_type(message, nullptr, &contents), "parse property type");
    const std::string signature = contents != nullptr ? contents : "";

    if (signature == "s" || signature == "o") {
      const char* value = nullptr;
      Check(sd_bus_message_read(message, "v", signature.c_str(), &value), "parse string property");
      properties.strings[name] = value != nullptr ? value : "";
    }
    else if (signature == "b") {
      int value = 0;
      Check(sd_bus_message_read(message, "v", "b", &value), "parse boolean property");
      properties.bools[name] = value != 0;
    }
    else if (signature == "n") {
      std::int16_t value = 0;
      Check(sd_bus_message_read(message, "v", "n", &value), "parse int16 property");
      properties.numbers[name] = value;
    }
    else if (signature == "q") {
      std::uint16_t value = 0;
      Check(sd_bus_message_read(message, "v", "q", &value), "parse uint16 property");
      properties.numbers[name] = value;
    }
    else {
      Check(sd_bus_message_skip(message, "v"), "skip property");
    }
    Check(sd_bus_message_exit_container(message), "parse properties");
  }
  Check(result, "parse properties");
  Check(sd_bus_message_exit_container(message), "parse properties");
}

/// Reads the a{oa{sa{sv}}} reply of ObjectManager.GetManagedObjects.
ManagedObjects ReadManagedObjects(sd_bus_message* message)
{
  ManagedObjects objects;
  Check(sd_bus_message_enter_container(message, SD_BUS_TYPE_ARRAY, "{oa{sa{sv}}}"), "parse objects");
  int result;
  while ((result = sd_bus_message_enter_container(message, SD_BUS_TYPE_DICT_ENTRY, "oa{sa{sv}}")) > 0) {
    const char* path = nullptr;
    Check(sd_bus_message_read(message, "o", &path), "parse object path");
    Interfaces& interfaces = objects[path];

    Check(sd_bus_message_enter_container(message, SD_BUS_TYPE_ARRAY, "{sa{sv}}"), "parse interfaces");
    int inner;
    while ((inner = sd_bus_message_enter_container(message, SD_BUS_TYPE_DICT_ENTRY, "sa{sv}")) > 0) {
      const char* interface = nullptr;
      Check(sd_bus_message_read(message, "s", &interface), "parse interface name");
      ReadProperties(message, interfaces[interface]);
      Check(sd_bus_message_exit_container(message), "parse interfaces");
    }
    Check(inner, "parse interfaces");
    Check(sd_bus_message_exit_container(message), "parse interfaces");
    Check(sd_bus_message_exit_container(message), "parse objects");
  }
  Check(result, "parse objects");
  Check(sd_bus_message_exit_container(message), "parse objects");
  return objects;
}

// ---------------------------------------------------------------------------
// IBluetoothDevice implementation
// ---------------------------------------------------------------------------

class BluetoothDeviceLinux final : public IBluetoothDevice {
public:
  BluetoothDeviceLinux()
  {
    sd_bus* bus = nullptr;
    Check(sd_bus_open_system(&bus), "cannot connect to the D-Bus system bus");
    bus_.reset(bus);
  }

  ~BluetoothDeviceLinux() override { Close(); }

  std::vector<DeviceInfo> Scan(std::chrono::milliseconds timeout) override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string adapter = FindAdapter();

    // Report LE devices only. Older BlueZ versions lack the filter: not fatal.
    try {
      Call(adapter, kAdapterInterface, "SetDiscoveryFilter", [](sd_bus_message* m) {
        Check(sd_bus_message_open_container(m, SD_BUS_TYPE_ARRAY, "{sv}"), "build filter");
        Check(sd_bus_message_append(m, "{sv}", "Transport", "s", "le"), "build filter");
        Check(sd_bus_message_close_container(m), "build filter");
      });
    }
    catch (const BluetoothError&) {
    }

    // Another application may already be discovering: that is fine for us.
    Call(adapter, kAdapterInterface, "StartDiscovery", {}, kCallTimeout, "org.bluez.Error.InProgress");
    std::this_thread::sleep_for(timeout);
    const ManagedObjects objects = GetManagedObjects();
    try {
      Call(adapter, kAdapterInterface, "StopDiscovery");
    }
    catch (const BluetoothError&) {
    }

    std::vector<DeviceInfo> devices;
    knownDevices_.clear();
    for (const auto& [path, interfaces] : objects) {
      const auto device = interfaces.find(kDeviceInterface);
      if (device == interfaces.end() || path.rfind(adapter + "/", 0) != 0)
        continue;
      const Properties& properties = device->second;
      // BlueZ also lists devices remembered from earlier sessions. RSSI is only
      // present for devices heard during this discovery.
      if (!properties.Has("RSSI") && !properties.Bool("Connected"))
        continue;
      const std::string address = properties.String("Address");
      knownDevices_[address] = path;
      devices.push_back(DeviceInfo{address, properties.String("Name")});
    }
    return devices;
  }

  void Connect(const std::string& address) override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    CloseLocked();

    if (!IsValidAddress(address))
      throw BluetoothError("invalid Bluetooth address: " + address);
    const std::string adapter = FindAdapter();

    std::string path;
    const auto known = knownDevices_.find(address);
    if (known != knownDevices_.end()) {
      path = known->second;
    }
    else {
      std::string suffix = address;
      std::replace(suffix.begin(), suffix.end(), ':', '_');
      path = adapter + "/dev_" + suffix;
    }

    // Fails with org.freedesktop.DBus.Error.UnknownObject if BlueZ never saw the device.
    Call(path, kDeviceInterface, "Connect", {}, kConnectTimeout);
    devicePath_ = path;

    try {
      WaitServicesResolved();
      FindCharacteristics();
      Subscribe();
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

  bool IsConnected() const override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    Pump();
    return connected_;
  }

  void Write(const std::uint8_t* data, std::size_t length) override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    Pump();
    if (!connected_)
      throw BluetoothError("write on a closed connection");

    for (std::size_t offset = 0; offset < length; offset += writeChunk_) {
      const std::size_t size = std::min(writeChunk_, length - offset);
      // No "type" option: BlueZ uses a write request when the characteristic
      // supports it, a write command otherwise.
      Call(rxPath_, kCharacteristicInterface, "WriteValue", [&](sd_bus_message* m) {
        Check(sd_bus_message_append_array(m, 'y', data + offset, size), "build write");
        Check(sd_bus_message_open_container(m, SD_BUS_TYPE_ARRAY, "{sv}"), "build write");
        Check(sd_bus_message_close_container(m), "build write");
      });
    }
  }

  std::size_t Read(std::uint8_t* buffer, std::size_t length, std::chrono::milliseconds timeout) override
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
      Pump();
      const std::size_t count = rx_.Pop(buffer, length, 0ms);  // throws once the connection is closed
      if (count > 0)
        return count;
      const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - std::chrono::steady_clock::now());
      if (remaining <= 0ms)
        return 0;
      sd_bus_wait(bus_.get(), ToMicroseconds(remaining));
    }
  }

private:
  using Appender = std::function<void(sd_bus_message*)>;

  /// Calls a BlueZ method and returns its reply, or nullptr if it failed with `tolerated`.
  MessagePtr Call(const std::string& path, const char* interface, const char* method, const Appender& append = {},
                  std::chrono::milliseconds timeout = kCallTimeout, const char* tolerated = nullptr) const
  {
    sd_bus_message* raw = nullptr;
    Check(sd_bus_message_new_method_call(bus_.get(), &raw, kBluez, path.c_str(), interface, method),
          "create D-Bus call");
    MessagePtr call(raw);
    if (append)
      append(call.get());

    BusError error;
    sd_bus_message* reply = nullptr;
    const int result = sd_bus_call(bus_.get(), call.get(), ToMicroseconds(timeout), error.get(), &reply);
    if (result < 0) {
      if (tolerated != nullptr && error.Is(tolerated))
        return nullptr;
      throw BluetoothError(std::string(method) + " failed: " + error.Describe(result));
    }
    return MessagePtr(reply);
  }

  ManagedObjects GetManagedObjects() const
  {
    MessagePtr reply = Call("/", kObjectManagerInterface, "GetManagedObjects");
    return ReadManagedObjects(reply.get());
  }

  Properties GetProperties(const std::string& path, const char* interface) const
  {
    MessagePtr reply = Call(path, kPropertiesInterface, "GetAll", [interface](sd_bus_message* m) {
      Check(sd_bus_message_append(m, "s", interface), "build GetAll");
    });
    Properties properties;
    ReadProperties(reply.get(), properties);
    return properties;
  }

  /// Returns the object path of the first powered adapter (usually /org/bluez/hci0).
  std::string FindAdapter() const
  {
    const ManagedObjects objects = GetManagedObjects();
    std::string poweredOff;
    for (const auto& [path, interfaces] : objects) {
      const auto adapter = interfaces.find(kAdapterInterface);
      if (adapter == interfaces.end())
        continue;
      if (adapter->second.Bool("Powered"))
        return path;
      poweredOff = path;
    }
    if (!poweredOff.empty())
      throw BluetoothError("Bluetooth adapter " + poweredOff + " is powered off");
    throw BluetoothError("no Bluetooth adapter found (is bluetoothd running?)");
  }

  void WaitServicesResolved()
  {
    const auto deadline = std::chrono::steady_clock::now() + kServicesTimeout;
    while (std::chrono::steady_clock::now() < deadline) {
      const Properties device = GetProperties(devicePath_, kDeviceInterface);
      if (!device.Bool("Connected"))
        throw BluetoothError("device disconnected during service discovery");
      if (device.Bool("ServicesResolved"))
        return;
      std::this_thread::sleep_for(100ms);
    }
    throw BluetoothError("service discovery timed out");
  }

  void FindCharacteristics()
  {
    const ManagedObjects objects = GetManagedObjects();
    for (const auto& [path, interfaces] : objects) {
      const auto characteristic = interfaces.find(kCharacteristicInterface);
      if (characteristic == interfaces.end() || path.rfind(devicePath_ + "/", 0) != 0)
        continue;
      const Properties& properties = characteristic->second;
      const std::string uuid = properties.String("UUID");
      if (EqualsIgnoreCase(uuid, gatt::kRxCharacteristicUuid)) {
        rxPath_ = path;
        // "MTU" exists since BlueZ 5.62; payload = MTU - 3 bytes of ATT header.
        const auto mtu = properties.numbers.find("MTU");
        if (mtu != properties.numbers.end() && mtu->second > 3)
          writeChunk_ = std::max<std::size_t>(gatt::kDefaultWriteChunk, static_cast<std::size_t>(mtu->second - 3));
      }
      else if (EqualsIgnoreCase(uuid, gatt::kTxCharacteristicUuid)) {
        txPath_ = path;
      }
    }
    if (rxPath_.empty() || txPath_.empty())
      throw BluetoothError("not a LumiLink device: LED service not found");
  }

  void Subscribe()
  {
    sd_bus_slot* slot = nullptr;
    Check(sd_bus_match_signal(bus_.get(), &slot, kBluez, txPath_.c_str(), kPropertiesInterface, "PropertiesChanged",
                              &BluetoothDeviceLinux::OnTxChanged, this),
          "subscribe to notifications");
    txSlot_.reset(slot);

    slot = nullptr;
    Check(sd_bus_match_signal(bus_.get(), &slot, kBluez, devicePath_.c_str(), kPropertiesInterface,
                              "PropertiesChanged", &BluetoothDeviceLinux::OnDeviceChanged, this),
          "watch connection state");
    deviceSlot_.reset(slot);

    rx_.Open();  // before StartNotify, so the first notification cannot be lost
    Call(txPath_, kCharacteristicInterface, "StartNotify");
    connected_ = true;
  }

  /// Dispatches pending signals. Never throws: a broken bus means a lost connection.
  void Pump() const
  {
    int result;
    while ((result = sd_bus_process(bus_.get(), nullptr)) > 0) {
    }
    if (result < 0 && connected_)
      MarkDisconnected();
  }

  void MarkDisconnected() const
  {
    connected_ = false;
    rx_.Close();
  }

  void CloseLocked() noexcept
  {
    if (!devicePath_.empty()) {
      try {
        if (connected_ && !txPath_.empty())
          Call(txPath_, kCharacteristicInterface, "StopNotify");
      }
      catch (...) {
      }
      try {
        Call(devicePath_, kDeviceInterface, "Disconnect");
      }
      catch (...) {
      }
    }
    txSlot_.reset();
    deviceSlot_.reset();
    devicePath_.clear();
    rxPath_.clear();
    txPath_.clear();
    writeChunk_ = gatt::kDefaultWriteChunk;
    MarkDisconnected();
  }

  // sd-bus callbacks run inside Pump(), i.e. on the calling thread with mutex_ held.
  // They must not throw: C code is on the stack.

  /// PropertiesChanged(s interface, a{sv} changed, as invalidated) on the TX characteristic.
  static int OnTxChanged(sd_bus_message* message, void* userdata, sd_bus_error* /*error*/)
  {
    auto* self = static_cast<BluetoothDeviceLinux*>(userdata);
    try {
      const char* interface = nullptr;
      Check(sd_bus_message_read(message, "s", &interface), "parse notification");
      Check(sd_bus_message_enter_container(message, SD_BUS_TYPE_ARRAY, "{sv}"), "parse notification");
      while (sd_bus_message_enter_container(message, SD_BUS_TYPE_DICT_ENTRY, "sv") > 0) {
        const char* key = nullptr;
        Check(sd_bus_message_read(message, "s", &key), "parse notification");
        if (std::strcmp(key, "Value") == 0) {
          Check(sd_bus_message_enter_container(message, SD_BUS_TYPE_VARIANT, "ay"), "parse notification");
          const void* bytes = nullptr;
          std::size_t size = 0;
          Check(sd_bus_message_read_array(message, 'y', &bytes, &size), "parse notification");
          self->rx_.Push(static_cast<const std::uint8_t*>(bytes), size);
          Check(sd_bus_message_exit_container(message), "parse notification");
        }
        else {
          Check(sd_bus_message_skip(message, "v"), "parse notification");
        }
        Check(sd_bus_message_exit_container(message), "parse notification");
      }
    }
    catch (...) {
      // A malformed signal is ignored; the next notification may still be fine.
    }
    return 0;
  }

  /// PropertiesChanged on the device object: detects disconnections.
  static int OnDeviceChanged(sd_bus_message* message, void* userdata, sd_bus_error* /*error*/)
  {
    auto* self = static_cast<BluetoothDeviceLinux*>(userdata);
    try {
      const char* interface = nullptr;
      Check(sd_bus_message_read(message, "s", &interface), "parse device change");
      if (std::strcmp(interface, kDeviceInterface) != 0)
        return 0;
      Properties changed;
      ReadProperties(message, changed);
      const auto connected = changed.bools.find("Connected");
      if (connected != changed.bools.end() && !connected->second)
        self->MarkDisconnected();
    }
    catch (...) {
    }
    return 0;
  }

  mutable std::mutex mutex_;
  BusPtr bus_;
  SlotPtr txSlot_;
  SlotPtr deviceSlot_;
  std::map<std::string, std::string> knownDevices_;  ///< address -> object path, from the last scan
  std::string devicePath_;
  std::string rxPath_;
  std::string txPath_;
  std::size_t writeChunk_ = gatt::kDefaultWriteChunk;
  mutable bool connected_ = false;
  mutable ReceiveBuffer rx_;
};

}  // namespace

std::unique_ptr<IBluetoothDevice> CreateBluetoothDevice()
{
  return std::make_unique<BluetoothDeviceLinux>();
}

}  // namespace lumilink::pal
