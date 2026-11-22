/**
 * @file tests/unit/test_texture_lock.cpp
 * @brief Verify ownership and failure handling for shared Windows capture textures.
 */
#ifdef _WIN32

#include <gtest/gtest.h>
#include <stdexcept>

#include "src/platform/windows/texture_lock.h"

namespace {
  struct fake_mutex {
    HRESULT acquire_result = S_OK;
    HRESULT release_result = S_OK;
    unsigned releases = 0;

    HRESULT AcquireSync(UINT64 key, DWORD timeout) {
      EXPECT_EQ(key, 0u);
      EXPECT_GT(timeout, 0u);
      EXPECT_LE(timeout, 500u);
      return acquire_result;
    }
    HRESULT ReleaseSync(UINT64 key) {
      EXPECT_EQ(key, 0u);
      ++releases;
      return release_result;
    }
  };

  TEST(TextureLock, FailedAcquireDoesNotUseOrReleaseTexture) {
    const HRESULT failures[] = {WAIT_TIMEOUT, WAIT_ABANDONED, DXGI_ERROR_DEVICE_REMOVED};
    for (const auto failure : failures) {
      fake_mutex mutex;
      mutex.acquire_result = failure;
      auto result = platf::dxgi::with_texture_lock(mutex, [] { ADD_FAILURE() << "Accessed unowned texture"; });
      EXPECT_EQ(result, failure);
      EXPECT_EQ(mutex.releases, 0u);
    }
  }

  TEST(TextureLock, ReturnsReleaseResultAfterAccess) {
    const HRESULT results[] = {S_OK, E_FAIL};
    for (const auto result : results) {
      fake_mutex mutex;
      mutex.release_result = result;
      unsigned accesses = 0;
      EXPECT_EQ(platf::dxgi::with_texture_lock(mutex, [&] {
        EXPECT_EQ(mutex.releases, 0u);
        ++accesses;
      }), result);
      EXPECT_EQ(accesses, 1u);
      EXPECT_EQ(mutex.releases, 1u);
    }
  }

  TEST(TextureLock, ReleasesTextureWhenAccessThrows) {
    fake_mutex mutex;
    EXPECT_THROW(platf::dxgi::with_texture_lock(mutex, [] { throw std::runtime_error("access failed"); }), std::runtime_error);
    EXPECT_EQ(mutex.releases, 1u);
  }
}  // namespace
#endif
