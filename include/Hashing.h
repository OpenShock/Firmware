#pragma once

#include <mbedtls/md5.h>
#include <mbedtls/sha1.h>
#include <mbedtls/sha256.h>

#include <array>

namespace OpenShock {
struct MD5 {
  MD5() { mbedtls_md5_init(&ctx); }
  ~MD5() { mbedtls_md5_free(&ctx); }

  bool begin() { return mbedtls_md5_starts_ret(&ctx) == 0; }
  bool update(const std::uint8_t* data, std::size_t dataLen) { return mbedtls_md5_update_ret(&ctx, data, dataLen) == 0; }
  bool finish(std::array<std::uint8_t, 16>& hash) { return mbedtls_md5_finish_ret(&ctx, hash.data()) == 0; }

  mbedtls_md5_context ctx;
};
struct SHA1 {
  SHA1() { mbedtls_sha1_init(&ctx); }
  ~SHA1() { mbedtls_sha1_free(&ctx); }

  bool begin() { return mbedtls_sha1_starts_ret(&ctx) == 0; }
  bool update(const std::uint8_t* data, std::size_t dataLen) { return mbedtls_sha1_update_ret(&ctx, data, dataLen) == 0; }
  bool finish(std::array<std::uint8_t, 20>& hash) { return mbedtls_sha1_finish_ret(&ctx, hash.data()) == 0; }

  mbedtls_sha1_context ctx;
};
struct SHA256 {
  SHA256() { mbedtls_sha256_init(&ctx); }
  ~SHA256() { mbedtls_sha256_free(&ctx); }

  bool begin() { return mbedtls_sha256_starts_ret(&ctx, 0) == 0; }
  bool update(const std::uint8_t* data, std::size_t dataLen) { return mbedtls_sha256_update_ret(&ctx, data, dataLen) == 0; }
  bool finish(std::array<std::uint8_t, 32>& hash) { return mbedtls_sha256_finish_ret(&ctx, hash.data()) == 0; }

  mbedtls_sha256_context ctx;
};
} // namespace OpenShock
