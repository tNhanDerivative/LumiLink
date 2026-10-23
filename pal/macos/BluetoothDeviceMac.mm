// macOS PAL: Bluetooth Low Energy through CoreBluetooth.
//
// Threading model: CoreBluetooth delivers every delegate callback on a private
// serial dispatch queue. Calls into CoreBluetooth are made on that same queue
// (dispatch_sync), and the blocking IBluetoothDevice methods wait on a
// condition variable that the delegate signals.
//
// Device addresses are CBPeripheral identifiers (UUID strings): macOS never
// exposes BLE MAC addresses.
//
// Applications must declare NSBluetoothAlwaysUsageDescription in their
// Info.plist, otherwise macOS denies (or kills the process on) Bluetooth access.

#import <CoreBluetooth/CoreBluetooth.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "BluetoothError.hpp"
#include "IBluetoothDevice.hpp"
#include "common/LumiLinkGatt.hpp"
#include "common/ReceiveBuffer.hpp"

using namespace std::chrono_literals;

namespace {

constexpr auto kPowerOnTimeout = 3s;
constexpr auto kConnectTimeout = 15s;
constexpr auto kDisconnectTimeout = 2s;
constexpr auto kWriteTimeout = 5s;

enum class Link { Idle, Connecting, Ready, Disconnecting, Failed };

/// State shared by the C++ object (caller threads) and the delegate (CoreBluetooth queue).
/**
 * Owned through a shared_ptr by both sides, so callbacks still in flight after
 * the device object is destroyed never touch freed memory.
 */
struct SharedState {
  std::mutex mutex;
  std::condition_variable cv;

  bool powerKnown = false;
  CBManagerState power = CBManagerStateUnknown;

  Link link = Link::Idle;
  std::string linkError;

  bool writePending = false;
  std::string writeError;

  lumilink::pal::ReceiveBuffer rx;

  void Fail(const std::string& error)
  {
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (link == Link::Connecting) {
        link = Link::Failed;
        linkError = error;
      }
    }
    cv.notify_all();
  }
};

std::string Describe(NSError* error)
{
  return error ? std::string(error.localizedDescription.UTF8String) : std::string("unknown error");
}

CBUUID* Uuid(const char* text)
{
  return [CBUUID UUIDWithString:@(text)];
}

}  // namespace

// ---------------------------------------------------------------------------
// Delegate: runs on the CoreBluetooth queue only.
// ---------------------------------------------------------------------------

// Hidden: Objective-C classes are exported by default, even with -fvisibility=hidden.
__attribute__((visibility("hidden")))
@interface LumiLinkCentralDelegate : NSObject <CBCentralManagerDelegate, CBPeripheralDelegate>
- (instancetype)initWithState:(std::shared_ptr<SharedState>)state;
// CoreBluetooth drops peripherals nobody references: keep every discovered one alive.
@property(nonatomic, strong) NSMutableDictionary<NSUUID*, CBPeripheral*>* discovered;
@property(nonatomic, strong) NSMutableDictionary<NSUUID*, NSString*>* names;
@property(nonatomic, strong) CBPeripheral* peripheral;
@property(nonatomic, strong) CBCharacteristic* rxCharacteristic;
@property(nonatomic, strong) CBCharacteristic* txCharacteristic;
@end

@implementation LumiLinkCentralDelegate {
  std::shared_ptr<SharedState> _state;
}

- (instancetype)initWithState:(std::shared_ptr<SharedState>)state
{
  if ((self = [super init])) {
    _state = std::move(state);
    _discovered = [NSMutableDictionary dictionary];
    _names = [NSMutableDictionary dictionary];
  }
  return self;
}

- (void)centralManagerDidUpdateState:(CBCentralManager*)central
{
  {
    std::lock_guard<std::mutex> lock(_state->mutex);
    _state->powerKnown = true;
    _state->power = central.state;
  }
  if (central.state != CBManagerStatePoweredOn) {
    _state->Fail("Bluetooth adapter is no longer available");
    _state->rx.Close();
  }
  _state->cv.notify_all();
}

- (void)centralManager:(CBCentralManager*)central
    didDiscoverPeripheral:(CBPeripheral*)peripheral
        advertisementData:(NSDictionary<NSString*, id>*)advertisementData
                     RSSI:(NSNumber*)RSSI
{
  (void)central;
  (void)RSSI;
  self.discovered[peripheral.identifier] = peripheral;
  // The advertised local name is more reliable than the cached GAP name.
  NSString* name = advertisementData[CBAdvertisementDataLocalNameKey];
  if (name == nil)
    name = peripheral.name;
  if (name.length > 0)
    self.names[peripheral.identifier] = name;
}

- (void)centralManager:(CBCentralManager*)central didConnectPeripheral:(CBPeripheral*)peripheral
{
  (void)central;
  if (peripheral != self.peripheral)
    return;
  peripheral.delegate = self;
  [peripheral discoverServices:@[ Uuid(lumilink::pal::gatt::kServiceUuid) ]];
}

- (void)centralManager:(CBCentralManager*)central
    didFailToConnectPeripheral:(CBPeripheral*)peripheral
                         error:(NSError*)error
{
  (void)central;
  if (peripheral == self.peripheral)
    _state->Fail("connection failed: " + Describe(error));
}

- (void)centralManager:(CBCentralManager*)central
    didDisconnectPeripheral:(CBPeripheral*)peripheral
                      error:(NSError*)error
{
  (void)central;
  if (peripheral != self.peripheral && self.peripheral != nil)
    return;
  {
    std::lock_guard<std::mutex> lock(_state->mutex);
    if (_state->link == Link::Connecting) {
      _state->link = Link::Failed;
      _state->linkError = "disconnected while connecting: " + Describe(error);
    }
    else {
      _state->link = Link::Idle;
    }
    if (_state->writePending) {
      _state->writePending = false;
      _state->writeError = "device disconnected";
    }
  }
  _state->rx.Close();
  _state->cv.notify_all();
}

- (void)peripheral:(CBPeripheral*)peripheral didDiscoverServices:(NSError*)error
{
  if (error) {
    _state->Fail("service discovery failed: " + Describe(error));
    return;
  }
  CBUUID* serviceUuid = Uuid(lumilink::pal::gatt::kServiceUuid);
  for (CBService* service in peripheral.services) {
    if ([service.UUID isEqual:serviceUuid]) {
      [peripheral discoverCharacteristics:@[
        Uuid(lumilink::pal::gatt::kRxCharacteristicUuid), Uuid(lumilink::pal::gatt::kTxCharacteristicUuid)
      ]
                               forService:service];
      return;
    }
  }
  _state->Fail("not a LumiLink device: LED service not found");
}

- (void)peripheral:(CBPeripheral*)peripheral
    didDiscoverCharacteristicsForService:(CBService*)service
                                   error:(NSError*)error
{
  if (error) {
    _state->Fail("characteristic discovery failed: " + Describe(error));
    return;
  }
  CBUUID* rxUuid = Uuid(lumilink::pal::gatt::kRxCharacteristicUuid);
  CBUUID* txUuid = Uuid(lumilink::pal::gatt::kTxCharacteristicUuid);
  for (CBCharacteristic* characteristic in service.characteristics) {
    if ([characteristic.UUID isEqual:rxUuid])
      self.rxCharacteristic = characteristic;
    else if ([characteristic.UUID isEqual:txUuid])
      self.txCharacteristic = characteristic;
  }
  if (self.rxCharacteristic == nil || self.txCharacteristic == nil) {
    _state->Fail("not a LumiLink device: RX/TX characteristics not found");
    return;
  }
  [peripheral setNotifyValue:YES forCharacteristic:self.txCharacteristic];
}

- (void)peripheral:(CBPeripheral*)peripheral
    didUpdateNotificationStateForCharacteristic:(CBCharacteristic*)characteristic
                                          error:(NSError*)error
{
  (void)peripheral;
  if (characteristic != self.txCharacteristic)
    return;
  if (error) {
    _state->Fail("cannot subscribe to notifications: " + Describe(error));
    return;
  }
  _state->rx.Open();  // before Ready, so no notification can be lost
  {
    std::lock_guard<std::mutex> lock(_state->mutex);
    if (_state->link == Link::Connecting)
      _state->link = Link::Ready;
  }
  _state->cv.notify_all();
}

- (void)peripheral:(CBPeripheral*)peripheral
    didUpdateValueForCharacteristic:(CBCharacteristic*)characteristic
                              error:(NSError*)error
{
  (void)peripheral;
  if (error || characteristic != self.txCharacteristic)
    return;
  NSData* value = characteristic.value;
  _state->rx.Push(static_cast<const std::uint8_t*>(value.bytes), value.length);
}

- (void)peripheral:(CBPeripheral*)peripheral
    didWriteValueForCharacteristic:(CBCharacteristic*)characteristic
                             error:(NSError*)error
{
  (void)peripheral;
  (void)characteristic;
  {
    std::lock_guard<std::mutex> lock(_state->mutex);
    _state->writePending = false;
    _state->writeError = error ? Describe(error) : std::string();
  }
  _state->cv.notify_all();
}

@end

// ---------------------------------------------------------------------------
// IBluetoothDevice implementation
// ---------------------------------------------------------------------------

namespace lumilink::pal {

namespace {

class BluetoothDeviceMac final : public IBluetoothDevice {
public:
  BluetoothDeviceMac() : state_(std::make_shared<SharedState>())
  {
    queue_ = dispatch_queue_create("com.lumilink.bluetooth", DISPATCH_QUEUE_SERIAL);
    delegate_ = [[LumiLinkCentralDelegate alloc] initWithState:state_];
    // The CBCentralManager is created lazily: creating it is what triggers the
    // Bluetooth permission request, which must not happen just by selecting BTManager.
  }

  ~BluetoothDeviceMac() override
  {
    Close();
    if (central_ == nil)
      return;
    dispatch_sync(queue_, ^{
      [central_ stopScan];
      central_.delegate = nil;
    });
  }

  std::vector<DeviceInfo> Scan(std::chrono::milliseconds timeout) override
  {
    WaitPoweredOn();
    dispatch_sync(queue_, ^{
      [delegate_.discovered removeAllObjects];
      [delegate_.names removeAllObjects];
      [central_ scanForPeripheralsWithServices:nil options:@{CBCentralManagerScanOptionAllowDuplicatesKey : @NO}];
    });

    std::this_thread::sleep_for(timeout);

    std::vector<DeviceInfo> devices;
    std::vector<DeviceInfo>* out = &devices;
    dispatch_sync(queue_, ^{
      [central_ stopScan];
      for (NSUUID* identifier in delegate_.discovered) {
        NSString* name = delegate_.names[identifier];
        out->push_back(DeviceInfo{identifier.UUIDString.UTF8String, name ? name.UTF8String : ""});
      }
    });
    return devices;
  }

  void Connect(const std::string& address) override
  {
    WaitPoweredOn();
    Close();

    NSUUID* identifier = [[NSUUID alloc] initWithUUIDString:@(address.c_str())];
    if (identifier == nil)
      throw BluetoothError("invalid device identifier: " + address);

    bool found = false;
    bool* foundPtr = &found;
    SharedState* state = state_.get();
    dispatch_sync(queue_, ^{
      CBPeripheral* peripheral = delegate_.discovered[identifier];
      if (peripheral == nil)
        peripheral = [central_ retrievePeripheralsWithIdentifiers:@[ identifier ]].firstObject;
      if (peripheral == nil)
        return;
      *foundPtr = true;
      delegate_.peripheral = peripheral;
      delegate_.rxCharacteristic = nil;
      delegate_.txCharacteristic = nil;
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->link = Link::Connecting;
        state->linkError.clear();
      }
      [central_ connectPeripheral:peripheral options:nil];
    });
    if (!found)
      throw BluetoothError("unknown device: " + address);

    std::unique_lock<std::mutex> lock(state_->mutex);
    const bool finished =
        state_->cv.wait_for(lock, kConnectTimeout, [this] { return state_->link != Link::Connecting; });
    if (finished && state_->link == Link::Ready)
      return;

    const std::string error = finished ? state_->linkError : "connection timed out";
    state_->link = Link::Idle;
    lock.unlock();
    Disconnect();
    throw BluetoothError(error);
  }

  void Close() noexcept override
  {
    Disconnect();
  }

  bool IsConnected() const override
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->link == Link::Ready;
  }

  void Write(const std::uint8_t* data, std::size_t length) override
  {
    if (!IsConnected())
      throw BluetoothError("write on a closed connection");

    // Prefer acknowledged writes: errors are reported and ordering is guaranteed.
    bool withResponse = false;
    std::size_t chunk = 0;
    bool* withResponsePtr = &withResponse;
    std::size_t* chunkPtr = &chunk;
    dispatch_sync(queue_, ^{
      CBCharacteristic* rx = delegate_.rxCharacteristic;
      if (rx == nil)
        return;
      *withResponsePtr = (rx.properties & CBCharacteristicPropertyWrite) != 0;
      const CBCharacteristicWriteType type =
          *withResponsePtr ? CBCharacteristicWriteWithResponse : CBCharacteristicWriteWithoutResponse;
      *chunkPtr = [delegate_.peripheral maximumWriteValueLengthForType:type];
    });
    if (chunk == 0)
      throw BluetoothError("write on a closed connection");

    for (std::size_t offset = 0; offset < length; offset += chunk) {
      const std::size_t size = std::min(chunk, length - offset);
      WriteChunk(data + offset, size, withResponse);
    }
  }

  std::size_t Read(std::uint8_t* buffer, std::size_t length, std::chrono::milliseconds timeout) override
  {
    return state_->rx.Pop(buffer, length, timeout);
  }

private:
  /// Checks the Bluetooth permission, then creates the central manager on first use.
  void EnsureCentral()
  {
    if (central_ != nil)
      return;

    // Without the usage description macOS aborts the whole process on first
    // Bluetooth access. Report it as an error instead of killing the host app.
    if ([NSBundle.mainBundle objectForInfoDictionaryKey:@"NSBluetoothAlwaysUsageDescription"] == nil)
      throw BluetoothError("NSBluetoothAlwaysUsageDescription is missing from the application's Info.plist");

    const CBManagerAuthorization authorization = CBManager.authorization;
    if (authorization == CBManagerAuthorizationDenied || authorization == CBManagerAuthorizationRestricted)
      throw BluetoothError(
          "Bluetooth permission denied: allow this application in System Settings > Privacy & Security > Bluetooth");

    central_ = [[CBCentralManager alloc] initWithDelegate:delegate_
                                                    queue:queue_
                                                  options:@{CBCentralManagerOptionShowPowerAlertKey : @NO}];
  }

  void WaitPoweredOn()
  {
    EnsureCentral();
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->cv.wait_for(lock, kPowerOnTimeout, [this] {
      return state_->powerKnown && state_->power != CBManagerStateUnknown && state_->power != CBManagerStateResetting;
    });
    switch (state_->power) {
    case CBManagerStatePoweredOn:
      return;
    case CBManagerStateUnauthorized:
      throw BluetoothError(
          "Bluetooth permission denied: allow this application in System Settings > Privacy & Security > Bluetooth");
    case CBManagerStatePoweredOff:
      throw BluetoothError("Bluetooth is turned off");
    case CBManagerStateUnsupported:
      throw BluetoothError("this Mac does not support Bluetooth Low Energy");
    default:
      throw BluetoothError("Bluetooth adapter is not available");
    }
  }

  void WriteChunk(const std::uint8_t* data, std::size_t size, bool withResponse)
  {
    if (withResponse) {
      std::lock_guard<std::mutex> lock(state_->mutex);
      state_->writePending = true;
      state_->writeError.clear();
    }

    bool sent = false;
    bool* sentPtr = &sent;
    NSData* value = [NSData dataWithBytes:data length:size];
    dispatch_sync(queue_, ^{
      if (delegate_.peripheral == nil || delegate_.rxCharacteristic == nil)
        return;
      [delegate_.peripheral writeValue:value
                     forCharacteristic:delegate_.rxCharacteristic
                                  type:withResponse ? CBCharacteristicWriteWithResponse
                                                    : CBCharacteristicWriteWithoutResponse];
      *sentPtr = true;
    });
    if (!sent)
      throw BluetoothError("device disconnected");
    if (!withResponse)
      return;

    std::unique_lock<std::mutex> lock(state_->mutex);
    if (!state_->cv.wait_for(lock, kWriteTimeout, [this] { return !state_->writePending; }))
      throw BluetoothError("write timed out");
    if (!state_->writeError.empty())
      throw BluetoothError("write failed: " + state_->writeError);
  }

  /// Cancels the connection (or pending connection) and waits for CoreBluetooth to confirm it.
  void Disconnect() noexcept
  {
    bool hadPeripheral = false;
    bool* hadPeripheralPtr = &hadPeripheral;
    SharedState* state = state_.get();
    dispatch_sync(queue_, ^{
      CBPeripheral* peripheral = delegate_.peripheral;
      if (peripheral == nil)
        return;
      *hadPeripheralPtr = true;
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->link = Link::Disconnecting;
      }
      if (delegate_.txCharacteristic != nil && peripheral.state == CBPeripheralStateConnected)
        [peripheral setNotifyValue:NO forCharacteristic:delegate_.txCharacteristic];
      [central_ cancelPeripheralConnection:peripheral];
    });
    state_->rx.Close();
    if (!hadPeripheral)
      return;

    // Waiting for didDisconnect prevents a late callback from breaking the next Connect().
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->cv.wait_for(lock, kDisconnectTimeout, [this] { return state_->link != Link::Disconnecting; });
    state_->link = Link::Idle;
    lock.unlock();
    dispatch_sync(queue_, ^{
      delegate_.peripheral = nil;
      delegate_.rxCharacteristic = nil;
      delegate_.txCharacteristic = nil;
    });
  }

  std::shared_ptr<SharedState> state_;
  dispatch_queue_t queue_;
  LumiLinkCentralDelegate* delegate_;
  CBCentralManager* central_;
};

}  // namespace

std::unique_ptr<IBluetoothDevice> CreateBluetoothDevice()
{
  return std::make_unique<BluetoothDeviceMac>();
}

}  // namespace lumilink::pal
