#pragma once

#include "Common.h"

#include <mbedtls/md5.h>
#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>

#include <array>  // TODO: When we use C++20, change this to <span>
#include <cstdint>
#include <string_view>

namespace OpenShock {
  class MD5 {
    DISABLE_COPY(MD5);
    DISABLE_MOVE(MD5);

  public:
    MD5() { mbedtls_md5_init(&ctx); }
    ~MD5() { mbedtls_md5_free(&ctx); }

    inline bool begin() { return mbedtls_md5_starts(&ctx) == 0; }
    inline bool update(const uint8_t* data, std::size_t dataLen) { return mbedtls_md5_update(&ctx, data, dataLen) == 0; }
    inline bool update(std::string_view data) { return update(reinterpret_cast<const uint8_t*>(data.data()), data.length()); }
    inline bool finish(std::array<uint8_t, 16>& hash) { return mbedtls_md5_finish(&ctx, hash.data()) == 0; }

  private:
    mbedtls_md5_context ctx;
  };
  class SHA1 {
    DISABLE_COPY(SHA1);
    DISABLE_MOVE(SHA1);

  public:
    SHA1() { mbedtls_sha1_init(&ctx); }
    ~SHA1() { mbedtls_sha1_free(&ctx); }

    inline bool begin() { return mbedtls_sha1_starts(&ctx) == 0; }
    inline bool update(const uint8_t* data, std::size_t dataLen) { return mbedtls_sha1_update(&ctx, data, dataLen) == 0; }
    inline bool update(std::string_view data) { return update(reinterpret_cast<const uint8_t*>(data.data()), data.length()); }
    inline bool finish(std::array<uint8_t, 20>& hash) { return mbedtls_sha1_finish(&ctx, hash.data()) == 0; }

  private:
    mbedtls_sha1_context ctx;
  };
  class SHA256 {
    DISABLE_COPY(SHA256);
    DISABLE_MOVE(SHA256);

  public:
    SHA256() { mbedtls_sha256_init(&ctx); }
    ~SHA256() { mbedtls_sha256_free(&ctx); }

    inline bool begin() { return mbedtls_sha256_starts(&ctx, 0) == 0; }
    inline bool update(const uint8_t* data, std::size_t dataLen) { return mbedtls_sha256_update(&ctx, data, dataLen) == 0; }
    inline bool update(std::string_view data) { return update(reinterpret_cast<const uint8_t*>(data.data()), data.length()); }
    inline bool finish(std::array<uint8_t, 32>& hash) { return mbedtls_sha256_finish(&ctx, hash.data()) == 0; }

  private:
    mbedtls_sha256_context ctx;
  };
}  // namespace OpenShock
