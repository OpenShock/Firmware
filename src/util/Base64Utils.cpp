#include "util/Base64Utils.h"

const char* const TAG = "Base64Utils";

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

bool Base64Utils::Encode(tcb::span<const uint8_t> data, std::string& output)
{
  std::size_t requiredLen = CalculateEncodedSize(data.size());

  // Allocate buffer for base64 string
  TinyVec<uint8_t> buffer(requiredLen);

  std::size_t written = 0;

  int retval = mbedtls_base64_encode(buffer.data(), buffer.size(), &written, data.data(), data.size());

  if (retval != 0) {
    if (retval == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)
      OS_LOGW(TAG, "Output buffer too small (expected %zu, got %zu)", written, buffer.size());
    else
      OS_LOGW(TAG, "Failed to encode data, error: %d", retval);
    output.clear();
    return false;
  }

  output.assign(reinterpret_cast<char*>(buffer.data()), written);
  return true;
}

bool Base64Utils::Decode(std::string_view data, TinyVec<uint8_t>& output) noexcept
{
  std::size_t requiredLen = CalculateDecodedSize(data.size());

  // Preallocate
  output.resize(requiredLen);

  std::size_t written = 0;

  int retval = mbedtls_base64_decode(output.data(), output.size(), &written, reinterpret_cast<const uint8_t*>(data.data()), data.size());

  if (retval != 0) {
    if (retval == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL)
      OS_LOGW(TAG, "Output buffer too small (expected %zu, got %zu)", written, output.size());
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
