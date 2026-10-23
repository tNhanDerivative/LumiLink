#pragma once

#include "DeviceManager.hpp"

namespace lumilink {

/// Debug transport: logs every order in a human-readable form, no device involved.
/**
 * Default manager of a new LumiLink instance. Always "connected".
 */
class TextManager : public DeviceManager {
public:
  const char* Name() const override { return "TextManager"; }

  Result FindDevice() override;  ///< Always Ok
  bool HasDevice() const override;
  Result Write(const Bytes& data) override;
  Result Execute(const Command& command) override;
};

}  // namespace lumilink
