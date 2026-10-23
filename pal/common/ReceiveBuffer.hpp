#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

#include "BluetoothError.hpp"

namespace lumilink::pal {

/// Thread-safe byte queue between the Bluetooth stack's notification thread and Read().
/**
 * Every OS delivers notifications on its own thread or event loop while
 * IBluetoothDevice::Read() is a blocking call with a timeout. This class is the
 * meeting point, shared by all native PAL implementations.
 */
class ReceiveBuffer {
public:
  /// Starts a new connection: drops stale bytes and accepts data again.
  void Open()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    bytes_.clear();
    open_ = true;
  }

  /// Connection lost or closed: wakes up any waiting Read().
  void Close()
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      open_ = false;
    }
    cv_.notify_all();
  }

  void Push(const std::uint8_t* data, std::size_t length)
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!open_)
        return;
      bytes_.insert(bytes_.end(), data, data + length);
    }
    cv_.notify_all();
  }

  /// Returns the number of bytes copied, 0 on timeout. Throws if the connection is closed.
  std::size_t Pop(std::uint8_t* buffer, std::size_t length, std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, timeout, [this] { return !bytes_.empty() || !open_; });
    if (bytes_.empty() && !open_)
      throw BluetoothError("read on a closed connection");
    std::size_t count = 0;
    while (count < length && !bytes_.empty()) {
      buffer[count++] = bytes_.front();
      bytes_.pop_front();
    }
    return count;
  }

private:
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<std::uint8_t> bytes_;
  bool open_ = false;
};

}  // namespace lumilink::pal
