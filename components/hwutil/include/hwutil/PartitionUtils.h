#pragma once

#include "util/FunctionRef.h"

#include <esp_partition.h>

#include <cstdint>
#include <string_view>

namespace OpenShock {
  namespace HTTP {
    class Client;
  }

  /// @brief The `static0` partition holding the captive portal's web assets (served by the portal, rewritten by OTA).
  const esp_partition_t* FindStaticPartition();

  bool TryGetPartitionHash(const esp_partition_t* partition, char (&hash)[65]);
  /// @param progressCallback Invoked with (written, total, fraction) while flashing; only used during this call, never stored.
  bool FlashPartitionFromUrl(HTTP::Client& client, const esp_partition_t* partition, std::string_view remoteUrl, const uint8_t (&remoteHash)[32], Util::FunctionRef<bool(std::size_t, std::size_t, float)> progressCallback);
}  // namespace OpenShock
