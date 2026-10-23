#pragma once

#include <stdexcept>

namespace lumilink::pal {

/// The only exception a PAL implementation is allowed to throw.
/**
 * BTManager catches it and turns it into a lumilink::Result code, so it never
 * reaches the public API.
 */
class BluetoothError : public std::runtime_error {
public:
  using std::runtime_error::runtime_error;
};

}  // namespace lumilink::pal
