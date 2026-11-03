#include "Base64.h"

const char* const TAG = "Base64";

#include "Logging.h"

#include <mbedtls/base64.h>

using namespace OpenShock;

constexpr std::size_t CalculateEncodedSize(std::size_t size) noexcept
{
  return 4 * ((size + 2) / 3) + 1;
}

constexpr std::size_t CalculateDecodedSize(std::size_t size) noexcept
{
  return ((size / 4) * 3) + 3;  // +3 guards against missing padding variants
}

bool Base64::Encode(std::span<const uint8_t> data, std::string& output)
{
  // Encode into a string of our own, then swap it in: one allocation, and `data` may view `output` itself, since
  // `output` is untouched until the encode is done. requiredLen counts mbedtls's NUL, which lands in the string's own
  // terminator slot once the size is trimmed to `written`.
  std::size_t requiredLen = CalculateEncodedSize(data.size());
  std::string encoded(requiredLen - 1, '\0');

  std::size_t written = 0;

  int retval = mbedtls_base64_encode(reinterpret_cast<uint8_t*>(encoded.data()), requiredLen, &written, data.data(), data.size());

  if (retval != 0) {
    if (retval == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)
      OS_LOGW(TAG, "Output buffer too small (expected %zu, got %zu)", written, requiredLen);
    else
      OS_LOGW(TAG, "Failed to encode data, error: %d", retval);
    output.clear();
    return false;
  }

  encoded.resize(written);
  output.swap(encoded);
  return true;
}

bool Base64::Decode(std::string_view data, std::vector<uint8_t>& output)
{
  std::size_t requiredLen = CalculateDecodedSize(data.size());

  // Preallocate
  output.resize(requiredLen);

  std::size_t written = 0;

  int retval = mbedtls_base64_decode(output.data(), output.size(), &written, reinterpret_cast<const uint8_t*>(data.data()), data.size());

  if (retval != 0) {
    if (retval == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)
      OS_LOGW(TAG, "Output buffer too small (expected %zu, got %zu)", written, static_cast<std::size_t>(output.size()));
    else if (retval == MBEDTLS_ERR_BASE64_INVALID_CHARACTER)
      OS_LOGW(TAG, "Invalid character in input data");
    else
      OS_LOGW(TAG, "Failed to decode data, error: %d", retval);

    output.clear();
    return false;
  }

  // Trim to actual size
  output.resize(written);
  return true;
}
