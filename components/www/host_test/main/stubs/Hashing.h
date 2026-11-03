#pragma once

// Host-test stub for the crypto component's Hashing.h, shadowing it on the include path. It exists only so that
// HostTest::FailNextHash can make the next begin() fail, as a real setup failure would, which makes
// Www::ParseError::HashFailed testable; it otherwise mirrors the real OpenShock::SHA256 API (compute/begin/update/finish).
#include <mbedtls/md.h>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace HostTest {
  inline bool FailNextHash = false;  // the next SHA256::begin() fails
}  // namespace HostTest

namespace OpenShock {
  class SHA256 {
  public:
    using Digest = std::array<uint8_t, 32>;

    /// @brief Hashes one contiguous buffer in a single call.
    [[nodiscard]] static inline bool compute(const uint8_t* data, std::size_t dataLen, Digest& hash) { return mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), data, dataLen, hash.data()) == 0; }

    bool begin()
    {
      m_data.clear();
      if (HostTest::FailNextHash) {
        HostTest::FailNextHash = false;
        return false;
      }
      return true;
    }
    bool update(const uint8_t* data, std::size_t dataLen)
    {
      m_data.insert(m_data.end(), data, data + dataLen);
      return true;
    }
    bool update(std::string_view data) { return update(reinterpret_cast<const uint8_t*>(data.data()), data.length()); }
    bool finish(Digest& hash) { return compute(m_data.data(), m_data.size(), hash); }

  private:
    std::vector<uint8_t> m_data;
  };
}  // namespace OpenShock
