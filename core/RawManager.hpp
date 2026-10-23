#pragma once

#include <iosfwd>

#include "DeviceManager.hpp"

namespace lumilink {

/// Writes the exact bytes that would go to the device to an output stream.
/**
 * Defaults to std::cout so the output can be piped to the device by any
 * other means (serial bridge, file, ...). Tests inject a std::ostringstream
 * to compare the byte stream against a golden master.
 */
class RawManager : public DeviceManager {
public:
  RawManager();
  explicit RawManager(std::ostream& out);

  const char* Name() const override { return "RawManager"; }

  Result FindDevice() override;  ///< Writes the handshake packet, always Ok (no reply can be read)
  bool HasDevice() const override;
  Result Write(const Bytes& data) override;

private:
  std::ostream& out_;
};

}  // namespace lumilink
