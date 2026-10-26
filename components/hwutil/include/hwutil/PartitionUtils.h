#pragma once

#include "util/FunctionRef.h"

#include <esp_partition.h>

#include <cstdint>
#include <string_view>

namespace OpenShock {
  namespace HTTP {
    class Client;
  }

  /// @brief The littlefs `static0` partition that holds the captive portal's assets on the 1.x partition layout, or null
  ///        on a 2.x layout (which has the flat `www` image instead, www/WwwPartition.h). 1.x compatibility.
  const esp_partition_t* FindStaticPartition();

  bool TryGetPartitionHash(const esp_partition_t* partition, char (&hash)[65]);
  /// @param progressCallback Invoked with (written, total, fraction) while flashing; only used during this call, never stored.
  bool FlashPartitionFromUrl(HTTP::Client& client, const esp_partition_t* partition, std::string_view remoteUrl, const uint8_t (&remoteHash)[32], Util::FunctionRef<bool(std::size_t, std::size_t, float)> progressCallback);
}  // namespace OpenShock
