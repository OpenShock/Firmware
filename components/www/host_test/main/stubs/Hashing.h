#pragma once

// Host-test stub for the crypto component's Hashing.h: the same incremental begin/update/finish API over mbedTLS, so
// HostTest::FailNextHash can make the next begin() fail, as a real setup failure would.
#include <mbedtls/md.h>

#include <array>
#include <cstdint>
#include <vector>

namespace HostTest {
  inline bool FailNextHash = false;  // the next SHA256::begin() fails
}  // namespace HostTest

namespace OpenShock {
  class SHA256 {
  public:
    using Digest = std::array<uint8_t, 32>;

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
    bool finish(Digest& hash) { return mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), m_data.data(), m_data.size(), hash.data()) == 0; }

  private:
    std::vector<uint8_t> m_data;
  };
}  // namespace OpenShock
