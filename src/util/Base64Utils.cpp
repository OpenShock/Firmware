#include "util/Base64Utils.h"

const char* const TAG = "Base64Utils";

#include "Logging.h"

#include <mbedtls/base64.h>

using namespace OpenShock;

std::size_t Base64Utils::Encode(const uint8_t* data, std::size_t dataLen, char* output, std::size_t outputLen) noexcept {
  std::size_t requiredLen = 0;

  int retval = mbedtls_base64_encode(reinterpret_cast<uint8_t*>(output), outputLen, &requiredLen, data, dataLen);
  if (retval != 0) {
    if (retval == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
      ESP_LOGW(TAG, "Output buffer too small (expected %zu, got %zu)", requiredLen, outputLen);
    } else {
      ESP_LOGW(TAG, "Failed to encode data, unknown error: %d", retval);
    }
    return 0;
  }

  return requiredLen;
}

bool Base64Utils::Encode(const uint8_t* data, std::size_t dataLen, std::string& output) {
  std::size_t requiredLen = Base64Utils::CalculateEncodedSize(dataLen) + 1; // +1 for null terminator
  char* buffer = new char[requiredLen];

  std::size_t written = Encode(data, dataLen, buffer, requiredLen);
  if (written == 0) {
    output.clear();
    delete[] buffer;
    return false;
  }

  buffer[written] = '\0';

  output.assign(buffer, written);
  
  delete[] buffer;

  if (written < requiredLen) {
    output.resize(written);
  }

  return true;
}

std::size_t Base64Utils::Decode(const char* data, std::size_t dataLen, uint8_t* output, std::size_t outputLen) noexcept {
  std::size_t requiredLen = 0;

  int retval = mbedtls_base64_decode(output, outputLen, &requiredLen, reinterpret_cast<const uint8_t*>(data), dataLen);
  if (retval != 0) {
    if (retval == MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
      ESP_LOGW(TAG, "Output buffer too small (expected %zu, got %zu)", requiredLen, outputLen);
    } else if (retval == MBEDTLS_ERR_BASE64_INVALID_CHARACTER) {
      ESP_LOGW(TAG, "Invalid character in input data");
    } else {
      ESP_LOGW(TAG, "Failed to decode data, unknown error: %d", retval);
    }
    return 0;
  }

  return requiredLen;
}

bool Base64Utils::Decode(const char* data, std::size_t dataLen, std::vector<uint8_t>& output) noexcept {
  std::size_t requiredLen = Base64Utils::CalculateDecodedSize(dataLen);
  output.resize(requiredLen);

  std::size_t written = Decode(data, dataLen, output.data(), output.size());
  if (written == 0) {
    output.clear();
    return false;
  }

  if (written < requiredLen) {
    output.resize(written);
  }

  return true;
}
