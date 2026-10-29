#pragma once

// A plain SHA-256 for the host tests, where mbedTLS (the crypto component) is not built for the linux target.
// Known-answer checked in test_www_image.cpp.
#include <cstddef>
#include <cstdint>
#include <span>

namespace HostTest {
  void Sha256(const uint8_t* data, std::size_t length, uint8_t (&digest)[32]);
  inline bool FailNextHash = false;  // the next SHA256::begin() fails (stubs/Hashing.h)
}  // namespace HostTest
