#include "LumiLink/LumiLink.hpp"

#include <mutex>
#include <sstream>

#include "DeviceManager.hpp"
#include "GroupManager.hpp"
#include "Log.hpp"
#include "ManagerFactory.hpp"

namespace lumilink {

struct LumiLink::Impl {
  mutable std::mutex mutex;
  ManagerFactory factory;
  std::unique_ptr<DeviceManager> device;
  GroupManager groups;

  Result Execute(const Command& command)
  {
    std::lock_guard<std::mutex> lock(mutex);
    Log::Info(Describe(command));
    return device->Execute(command);
  }

  Result ExecuteGroup(const std::string& name, const Command& command)
  {
    std::lock_guard<std::mutex> lock(mutex);
    Log::Info("Order for group '" + name + "'");
    return groups.Execute(name, command, *device);
  }
};

const char* ToString(Result result)
{
  switch (result) {
  case Result::Ok: return "ok";
  case Result::NotConnected: return "no device connected";
  case Result::WriteFailed: return "write to device failed";
  case Result::NoSuchGroup: return "no such group";
  case Result::GroupExists: return "group already exists";
  case Result::LedAlreadyInGroup: return "LED already in group";
  case Result::LedNotInGroup: return "LED not in group";
  case Result::NoSuchManager: return "no such device manager";
  case Result::DeviceNotFound: return "device not found";
  case Result::ConnectFailed: return "connection failed";
  case Result::HandshakeFailed: return "handshake failed";
  case Result::ScanFailed: return "Bluetooth scan failed";
  }
  return "unknown result";
}

LumiLink::LumiLink() : impl_(std::make_unique<Impl>())
{
  impl_->device = impl_->factory.Create("TextManager");
  std::ostringstream ss;
  ss << "[" << this << "] LumiLink started with " << impl_->device->Name();
  Log::Info(ss.str());
}

LumiLink::~LumiLink()
{
  std::ostringstream ss;
  ss << "[" << this << "] LumiLink stopped";
  Log::Info(ss.str());
}

Result LumiLink::ChangeDeviceManager(const std::string& name)
{
  std::unique_ptr<DeviceManager> next = impl_->factory.Create(name);
  if (!next)
    return Result::NoSuchManager;

  std::unique_ptr<DeviceManager> previous;
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    Log::Info(std::string("Device manager: ") + impl_->device->Name() + " -> " + next->Name());
    previous = std::move(impl_->device);
    impl_->device = std::move(next);
  }
  // The previous manager (and its connection) is released outside the lock.
  return Result::Ok;
}

std::string LumiLink::DeviceManagerName() const
{
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->device->Name();
}

Result LumiLink::FindDevice()
{
  std::lock_guard<std::mutex> lock(impl_->mutex);
  Log::Info(std::string("Finding device with ") + impl_->device->Name());
  return impl_->device->FindDevice();
}

bool LumiLink::HasDevice() const
{
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->device->HasDevice();
}

Result LumiLink::Status() const
{
  return HasDevice() ? Result::Ok : Result::NotConnected;
}

Result LumiLink::Write(const std::vector<std::uint8_t>& data)
{
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->device->Write(data);
}

Result LumiLink::SetColor(std::uint8_t led, Color color)
{
  return impl_->Execute(Command::MakeColor(led, color));
}

Result LumiLink::SetBrightness(std::uint8_t led, std::uint8_t brightness)
{
  return impl_->Execute(Command::MakeBrightness(led, brightness));
}

Result LumiLink::SetEffect(std::uint8_t led, Effect effect, std::uint8_t speed, Color color)
{
  return impl_->Execute(Command::MakeEffect(led, effect, speed, color));
}

Result LumiLink::TurnOff(std::uint8_t led)
{
  return impl_->Execute(Command::MakeTurnOff(led));
}

Result LumiLink::NewGroup(const std::string& name)
{
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->groups.NewGroup(name);
}

Result LumiLink::AddToGroup(const std::string& name, std::uint8_t led)
{
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->groups.AddToGroup(name, led);
}

Result LumiLink::RemoveFromGroup(const std::string& name, std::uint8_t led)
{
  std::lock_guard<std::mutex> lock(impl_->mutex);
  return impl_->groups.RemoveFromGroup(name, led);
}

Result LumiLink::SetGroupColor(const std::string& name, Color color)
{
  return impl_->ExecuteGroup(name, Command::MakeColor(0, color));
}

Result LumiLink::SetGroupBrightness(const std::string& name, std::uint8_t brightness)
{
  return impl_->ExecuteGroup(name, Command::MakeBrightness(0, brightness));
}

Result LumiLink::SetGroupEffect(const std::string& name, Effect effect, std::uint8_t speed, Color color)
{
  return impl_->ExecuteGroup(name, Command::MakeEffect(0, effect, speed, color));
}

Result LumiLink::TurnOffGroup(const std::string& name)
{
  return impl_->ExecuteGroup(name, Command::MakeTurnOff(0));
}

void LumiLink::SetLogMode(LogMode mode)
{
  Log::SetMode(mode);
}

LogMode LumiLink::GetLogMode()
{
  return Log::GetMode();
}

void LumiLink::SetLogFile(const std::string& path)
{
  Log::SetFile(path);
}

void LumiLink::SetLogCallback(LumiLinkLogCallback callback)
{
  Log::SetCallback(callback);
}

}  // namespace lumilink
