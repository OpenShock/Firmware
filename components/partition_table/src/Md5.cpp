#include "Md5.h"

#include <cstring>

namespace {
  constexpr uint32_t S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
  constexpr uint32_t K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
  };

  inline uint32_t rotl(uint32_t x, uint32_t c)
  {
    return (x << c) | (x >> (32 - c));
  }

  void transform(uint32_t state[4], const uint8_t block[64])
  {
    uint32_t m[16];
    for (int i = 0; i < 16; ++i) {
      m[i] = static_cast<uint32_t>(block[i * 4]) | (static_cast<uint32_t>(block[i * 4 + 1]) << 8) | (static_cast<uint32_t>(block[i * 4 + 2]) << 16) | (static_cast<uint32_t>(block[i * 4 + 3]) << 24);
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    for (uint32_t i = 0; i < 64; ++i) {
      uint32_t f, g;
      if (i < 16) {
        f = (b & c) | (~b & d);
        g = i;
      } else if (i < 32) {
        f = (d & b) | (~d & c);
        g = (5 * i + 1) % 16;
      } else if (i < 48) {
        f = b ^ c ^ d;
        g = (3 * i + 5) % 16;
      } else {
        f = c ^ (b | ~d);
        g = (7 * i) % 16;
      }
      uint32_t tmp = d;
      d            = c;
      c            = b;
      b            = b + rotl(a + f + K[i] + m[g], S[i]);
      a            = tmp;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
  }
}  // namespace

void OpenShock::PartitionTable::Internal::Md5(const uint8_t* data, std::size_t length, uint8_t (&digest)[16])
{
  uint32_t state[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
  std::size_t pos   = 0;
  for (; pos + 64 <= length; pos += 64) {
    transform(state, data + pos);
  }
  uint8_t block[64];
  std::size_t rest = length - pos;
  memcpy(block, data + pos, rest);
  block[rest] = 0x80;
  if (rest + 1 > 56) {
    memset(block + rest + 1, 0, 64 - rest - 1);
    transform(state, block);
    memset(block, 0, 56);
  } else {
    memset(block + rest + 1, 0, 56 - rest - 1);
  }
  uint64_t bits = static_cast<uint64_t>(length) * 8;
  for (int i = 0; i < 8; ++i) {
    block[56 + i] = static_cast<uint8_t>(bits >> (8 * i));
  }
  transform(state, block);
  for (int i = 0; i < 4; ++i) {
    digest[i * 4]     = static_cast<uint8_t>(state[i]);
    digest[i * 4 + 1] = static_cast<uint8_t>(state[i] >> 8);
    digest[i * 4 + 2] = static_cast<uint8_t>(state[i] >> 16);
    digest[i * 4 + 3] = static_cast<uint8_t>(state[i] >> 24);
  }
}
