#pragma once

#include "OpenShock.h"

// The low-level per-algorithm headers (mbedtls/md5.h, sha1.h, sha256.h) and their
// mbedtls_md5_* / mbedtls_sha1_* / mbedtls_sha256_* APIs became private in mbedTLS
// 3.6 (shipped with ESP-IDF 6.0). The generic message-digest interface in
// mbedtls/md.h is the public, stable replacement, so all hashers are built on it.
#include <mbedtls/md.h>

#include <array>
#include <cstdint>
#include <string_view>

namespace OpenShock {
  template<mbedtls_md_type_t Type, std::size_t DigestSize>
  class Hasher {
    DISABLE_COPY(Hasher);
    DISABLE_MOVE(Hasher);

  public:
    using Digest = std::array<uint8_t, DigestSize>;

    // A setup failure (algorithm not compiled in, out of memory) surfaces as begin() returning false.
    Hasher()
    {
      mbedtls_md_init(&m_ctx);
      m_setupOk = mbedtls_md_setup(&m_ctx, mbedtls_md_info_from_type(Type), 0) == 0;
    }
    ~Hasher() { mbedtls_md_free(&m_ctx); }

    inline bool begin() { return m_setupOk && mbedtls_md_starts(&m_ctx) == 0; }
    inline bool update(const uint8_t* data, std::size_t dataLen) { return mbedtls_md_update(&m_ctx, data, dataLen) == 0; }
    inline bool update(std::string_view data) { return update(reinterpret_cast<const uint8_t*>(data.data()), data.length()); }
    inline bool finish(Digest& hash) { return mbedtls_md_finish(&m_ctx, hash.data()) == 0; }

  private:
    mbedtls_md_context_t m_ctx;
    bool m_setupOk;
  };

  using MD5    = Hasher<MBEDTLS_MD_MD5, 16>;
  using SHA1   = Hasher<MBEDTLS_MD_SHA1, 20>;
  using SHA256 = Hasher<MBEDTLS_MD_SHA256, 32>;
}  // namespace OpenShock
