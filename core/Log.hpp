#pragma once

#include <string>

#include "LumiLink/LumiLink.hpp"

/// Process-wide logging with switchable outputs.
/**
 * Info messages are only emitted when the library is built with
 * LUMILINK_VERBOSE=ON, unless `force` is true. Errors are always emitted
 * (except in Silent mode).
 */
namespace lumilink::Log {

void SetMode(LogMode mode);
LogMode GetMode();
void SetFile(const std::string& path);
void SetCallback(LumiLinkLogCallback callback);

void Info(const std::string& message, bool force = false);
void Error(const std::string& message);

}  // namespace lumilink::Log
