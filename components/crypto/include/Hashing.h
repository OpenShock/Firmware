#pragma once

#include "OpenShock.h"

// The low-level per-algorithm headers (mbedtls/md5.h, sha1.h, sha256.h) and their
// mbedtls_md5_* / mbedtls_sha1_* / mbedtls_sha256_* APIs became private in mbedTLS
// 3.6 (shipped with ESP-IDF 6.0). The generic message-digest interface in
// mbedtls/md.h is the public, stable replacement, so all three hashers are now
// built on top of it.
#include <mbedtls/md.h>

#include <array>  // TODO: When we use C++20, change this to <span>
#include <cstdint>
#include <string_view>

namespace OpenShock {
  class MD5 {
    DISABLE_COPY(MD5);
    DISABLE_MOVE(MD5);

  public:
    // mbedtls_md_info_from_type never returns null for a compiled-in algorithm
    // (MD5/SHA1/SHA256 are all enabled by default); a setup failure surfaces as
    // begin() returning false.
    MD5()
    {
      mbedtls_md_init(&ctx);
      mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_MD5), 0);
    }
    ~MD5() { mbedtls_md_free(&ctx); }

    inline bool begin() { return mbedtls_md_starts(&ctx) == 0; }
    inline bool update(const uint8_t* data, std::size_t dataLen) { return mbedtls_md_update(&ctx, data, dataLen) == 0; }
    inline bool update(std::string_view data) { return update(reinterpret_cast<const uint8_t*>(data.data()), data.length()); }
    inline bool finish(std::array<uint8_t, 16>& hash) { return mbedtls_md_finish(&ctx, hash.data()) == 0; }

  private:
    mbedtls_md_context_t ctx;
  };
  class SHA1 {
    DISABLE_COPY(SHA1);
    DISABLE_MOVE(SHA1);

  public:
    SHA1()
    {
      mbedtls_md_init(&ctx);
      mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA1), 0);
    }
    ~SHA1() { mbedtls_md_free(&ctx); }

    inline bool begin() { return mbedtls_md_starts(&ctx) == 0; }
    inline bool update(const uint8_t* data, std::size_t dataLen) { return mbedtls_md_update(&ctx, data, dataLen) == 0; }
    inline bool update(std::string_view data) { return update(reinterpret_cast<const uint8_t*>(data.data()), data.length()); }
    inline bool finish(std::array<uint8_t, 20>& hash) { return mbedtls_md_finish(&ctx, hash.data()) == 0; }

  private:
    mbedtls_md_context_t ctx;
  };
  class SHA256 {
    DISABLE_COPY(SHA256);
    DISABLE_MOVE(SHA256);

  public:
    SHA256()
    {
      mbedtls_md_init(&ctx);
      mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
    }
    ~SHA256() { mbedtls_md_free(&ctx); }

    inline bool begin() { return mbedtls_md_starts(&ctx) == 0; }
    inline bool update(const uint8_t* data, std::size_t dataLen) { return mbedtls_md_update(&ctx, data, dataLen) == 0; }
    inline bool update(std::string_view data) { return update(reinterpret_cast<const uint8_t*>(data.data()), data.length()); }
    inline bool finish(std::array<uint8_t, 32>& hash) { return mbedtls_md_finish(&ctx, hash.data()) == 0; }

  private:
    mbedtls_md_context_t ctx;
  };
}  // namespace OpenShock
