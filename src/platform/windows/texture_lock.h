/**
 * @file src/platform/windows/texture_lock.h
 * @brief Synchronize access to a D3D texture shared between capture and encoding.
 */
#pragma once

#include <dxgi.h>

#include "src/utility.h"

namespace platf::dxgi {
  template<class Mutex, class Access>
  HRESULT with_texture_lock(Mutex &mutex, Access access) {
    // Allow transient GPU contention without trapping capture or shutdown behind a stalled device.
    constexpr DWORD timeout_ms = 500;
    HRESULT status = mutex.AcquireSync(0, timeout_ms);
    // WAIT_TIMEOUT and WAIT_ABANDONED both satisfy SUCCEEDED() but neither grants usable access.
    if (status != S_OK) {
      return status;
    }

    {
      auto unlock = util::fail_guard([&] { status = mutex.ReleaseSync(0); });
      access();
    }
    return status;
  }
}  // namespace platf::dxgi
