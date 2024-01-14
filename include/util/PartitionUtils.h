#pragma once

#include "StringView.h"

#include <esp_partition.h>

#include <cstdint>
#include <functional>

namespace OpenShock {
  bool TryGetPartitionHash(const esp_partition_t* partition, char (&hash)[65]);
  bool FlashPartitionFromUrl(const esp_partition_t* partition, StringView remoteUrl, const std::uint8_t (&remoteHash)[32], std::function<bool(std::size_t, std::size_t, float)> progressCallback = nullptr);
}
