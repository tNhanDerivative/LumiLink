#include "ManagerFactory.hpp"

#include "BTManager.hpp"
#include "Log.hpp"
#include "RawManager.hpp"
#include "TextManager.hpp"

namespace lumilink {

ManagerFactory::ManagerFactory()
{
  creators_["TextManager"] = [] { return std::make_unique<TextManager>(); };
  creators_["RawManager"] = [] { return std::make_unique<RawManager>(); };
  creators_["BTManager"] = [] { return std::make_unique<BTManager>(); };
}

std::unique_ptr<DeviceManager> ManagerFactory::Create(const std::string& name) const
{
  const auto it = creators_.find(name);
  if (it == creators_.end()) {
    Log::Error("No device manager named '" + name + "'");
    return nullptr;
  }
  return it->second();
}

std::vector<std::string> ManagerFactory::Names() const
{
  std::vector<std::string> names;
  for (const auto& entry : creators_)
    names.push_back(entry.first);
  return names;
}

}  // namespace lumilink
