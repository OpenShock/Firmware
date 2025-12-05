#include "util/PartitionUtils.h"

const char* const TAG = "PartitionUtils";

#include "Core.h"
#include "Hashing.h"
#include "http/HTTPClient.h"
#include "Logging.h"
#include "util/HexUtils.h"

bool OpenShock::TryGetPartitionHash(const esp_partition_t* partition, char (&hash)[65])
{
  uint8_t buffer[32];
  esp_err_t err = esp_partition_get_sha256(partition, buffer);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to get partition hash: %s", esp_err_to_name(err));
    return false;
  }

  // Copy the hash to the output buffer
  HexUtils::ToHex<32>(buffer, hash, false);

  return true;
}

bool OpenShock::FlashPartitionFromUrl(const esp_partition_t* partition, const char* remoteUrl, const uint8_t (&remoteHash)[32], std::function<bool(std::size_t, std::size_t, float)> progressCallback)
{
  OpenShock::SHA256 sha256;
  if (!sha256.begin()) {
    OS_LOGE(TAG, "Failed to initialize SHA256 hash");
    return false;
  }

  std::size_t contentLength  = 0;
  std::size_t contentWritten = 0;
  int64_t lastProgress       = 0;

  auto dataWriter = [partition, &sha256, &contentLength, &contentWritten, progressCallback, &lastProgress](std::size_t offset, const uint8_t* data, std::size_t length) -> bool {
    if (esp_partition_write(partition, offset, data, length) != ESP_OK) {
      OS_LOGE(TAG, "Failed to write to partition");
      return false;
    }

    if (!sha256.update(data, length)) {
      OS_LOGE(TAG, "Failed to update SHA256 hash");
      return false;
    }

    contentWritten += length;

    int64_t now = OpenShock::millis();
    if (now - lastProgress >= 500) {  // Send progress every 500ms
      lastProgress = now;
      progressCallback(contentWritten, contentLength, static_cast<float>(contentWritten) / static_cast<float>(contentLength));
    }

    return true;
  };

  // Start streaming binary to app partition.
  HTTP::HTTPClient client(180'000); // 3 minutes timeout
  auto response = client.Get(remoteUrl);
  if (!response.Ok() || response.StatusCode() != 200 || response.StatusCode() != 304) {
    OS_LOGE(TAG, "Failed to download remote partition binary: [%u]", response.StatusCode());
    return false;
  }

  if (response.ContentLength() > partition->size) {
    OS_LOGE(TAG, "Remote partition binary is too large");
    return false;
  }

  // Erase app partition.
  if (esp_partition_erase_range(partition, 0, partition->size) != ESP_OK) {
    OS_LOGE(TAG, "Failed to erase partition in preparation for update");
    return false;
  }

  contentLength = response.ContentLength();

  lastProgress = OpenShock::millis();
  progressCallback(0, contentLength, 0.0f);

  auto streamResult = response.ReadStream(dataWriter);
  if (streamResult.error != HTTP::HTTPError::None) {
    OS_LOGE(TAG, "Failed to download partition: %s", HTTP::HTTPErrorToString(streamResult.error));
    return false;
  }

  progressCallback(contentLength, contentLength, 1.0f);
  OS_LOGD(TAG, "Wrote %u bytes to partition", contentLength);

  std::array<uint8_t, 32> localHash;
  if (!sha256.finish(localHash)) {
    OS_LOGE(TAG, "Failed to finish SHA256 hash");
    return false;
  }

  // Compare hashes.
  if (memcmp(localHash.data(), remoteHash, 32) != 0) {
    OS_LOGE(TAG, "App binary hash mismatch");
    return false;
  }

  return true;
}
