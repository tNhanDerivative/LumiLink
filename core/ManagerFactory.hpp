#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "DeviceManager.hpp"

namespace lumilink {

/// Creates device managers by name, so the transport can be switched at runtime.
class ManagerFactory {
public:
  using Creator = std::function<std::unique_ptr<DeviceManager>()>;

  /// Registers TextManager, RawManager and BTManager.
  ManagerFactory();

  /// nullptr if no manager has this name.
  std::unique_ptr<DeviceManager> Create(const std::string& name) const;
  std::vector<std::string> Names() const;

private:
  std::map<std::string, Creator> creators_;
};

}  // namespace lumilink
