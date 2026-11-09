#pragma once

#include "util/FunctionRef.h"

#include <esp_partition.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace OpenShock {
  namespace HTTP {
    class Client;
  }
  class GzipInflater;

  /// @brief Streams `remoteUrl` into `partition`, erasing just ahead of the data, and checks the SHA-256 of what was
  ///        downloaded against `remoteHash`.
  /// @param progressCallback Invoked with (body bytes received, Content-Length, fraction) while flashing; only used during
  ///        this call, never stored.
  /// @param firstChunkCheck Invoked with the first chunk of the body before anything is erased: returning false aborts
  ///        the flash with the partition untouched, so a download that is not the kind of image the partition takes
  ///        never destroys what is there. Only used during this call, never stored. For a gzip body it sees the
  ///        first decompressed piece.
  /// @param inflater When given and valid, a body that starts like gzip is decompressed through it and `remoteHash`
  ///        is checked against the decompressed image. Any other body is flashed as is and the inflater released,
  ///        so its memory is back before the bulk of the download. Only used during this call, never stored.
  bool FlashPartitionFromUrl(
    HTTP::Client& client,
    const esp_partition_t* partition,
    const char* remoteUrl,
    const uint8_t (&remoteHash)[32],
    Util::FunctionRef<bool(std::size_t, std::size_t, float)> progressCallback,
    std::optional<Util::FunctionRef<bool(std::span<const uint8_t>)>> firstChunkCheck = std::nullopt,
    GzipInflater* inflater                                                           = nullptr
  );
}  // namespace OpenShock
