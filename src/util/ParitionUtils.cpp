#include "util/PartitionUtils.h"

#include "Hashing.h"
#include "http/HTTPRequestManager.h"
#include "Time.h"
#include "util/HexUtils.h"

#include <esp_log.h>

const char* const TAG = "PartitionUtils";

bool OpenShock::TryGetPartitionHash(const esp_partition_t* partition, char (&hash)[65]) {
  uint8_t buffer[32];
  esp_err_t err = esp_partition_get_sha256(partition, buffer);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get partition hash: %s", esp_err_to_name(err));
    return false;
  }

  // Copy the hash to the output buffer
  HexUtils::ToHex<32>(buffer, hash, false);

  return true;
}

bool OpenShock::FlashPartitionFromUrl(const esp_partition_t* partition, StringView remoteUrl, const uint8_t (&remoteHash)[32], std::function<bool(std::size_t, std::size_t, float)> progressCallback) {
  OpenShock::SHA256 sha256;
  if (!sha256.begin()) {
    ESP_LOGE(TAG, "Failed to initialize SHA256 hash");
    return false;
  }

  std::size_t contentLength  = 0;
  std::size_t contentWritten = 0;
  int64_t lastProgress  = 0;

  auto sizeValidator = [partition, &contentLength, progressCallback, &lastProgress](std::size_t size) -> bool {
    if (size > partition->size) {
      ESP_LOGE(TAG, "Remote partition binary is too large");
      return false;
    }

    // Erase app partition.
    if (esp_partition_erase_range(partition, 0, partition->size) != ESP_OK) {
      ESP_LOGE(TAG, "Failed to erase partition in preparation for update");
      return false;
    }

    contentLength = size;

    lastProgress = OpenShock::millis();
    progressCallback(0, contentLength, 0.0f);

    return true;
  };
  auto dataWriter = [partition, &sha256, &contentLength, &contentWritten, progressCallback, &lastProgress](std::size_t offset, const uint8_t* data, std::size_t length) -> bool {
    if (esp_partition_write(partition, offset, data, length) != ESP_OK) {
      ESP_LOGE(TAG, "Failed to write to partition");
      return false;
    }

    if (!sha256.update(data, length)) {
      ESP_LOGE(TAG, "Failed to update SHA256 hash");
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
  auto appBinaryResponse = OpenShock::HTTP::Download(
    remoteUrl,
    {
      {"Accept", "application/octet-stream"}
  },
    sizeValidator,
    dataWriter,
    {200, 304},
    180'000
  );  // 3 minutes
  if (appBinaryResponse.result != OpenShock::HTTP::RequestResult::Success) {
    ESP_LOGE(TAG, "Failed to download remote partition binary: [%u]", appBinaryResponse.code);
    return false;
  }

  progressCallback(contentLength, contentLength, 1.0f);
  ESP_LOGD(TAG, "Wrote %u bytes to partition", appBinaryResponse.data);

  std::array<uint8_t, 32> localHash;
  if (!sha256.finish(localHash)) {
    ESP_LOGE(TAG, "Failed to finish SHA256 hash");
    return false;
  }

  // Compare hashes.
  if (memcmp(localHash.data(), remoteHash, 32) != 0) {
    ESP_LOGE(TAG, "App binary hash mismatch");
    return false;
  }

  return true;
}
