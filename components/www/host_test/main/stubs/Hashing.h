#pragma once

// Host-test stub for the crypto component's Hashing.h, where mbedTLS is not built for the linux target: the same
// incremental begin/update/finish API over the plain SHA-256 in sha256.h. HostTest::FailNextHash makes the next
// begin() fail, as a real setup failure would.
#include "sha256.h"

#include <array>
#include <cstdint>
#include <vector>

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
    bool finish(Digest& hash)
    {
      uint8_t out[32];
      HostTest::Sha256(m_data.data(), m_data.size(), out);
      std::copy(out, out + 32, hash.begin());
      return true;
    }

  private:
    std::vector<uint8_t> m_data;
  };
}  // namespace OpenShock
