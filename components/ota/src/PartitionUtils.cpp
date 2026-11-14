#include "PartitionUtils.h"

const char* const TAG = "PartitionUtils";

#include "GzipInflater.h"
#include "Hashing.h"
#include "http/ContentTypes.h"
#include "http/HTTPRequestManager.h"
#include "Logging.h"

#include "Temporal.h"

#include <algorithm>
#include <cstring>

// Erased ahead of the data in steps of this much. Erasing the whole partition up front kept the flash cache off for
// seconds, starving the idle tasks into the task watchdog; one step of this size takes well under a second.
static constexpr std::size_t kEraseStep = 64 * 1024;

bool OpenShock::FlashPartitionFromUrl(
  HTTP::Client& client,
  const esp_partition_t* partition,
  const char* remoteUrl,
  const uint8_t (&remoteHash)[32],
  Util::FunctionRef<bool(std::size_t, std::size_t, float)> progressCallback,
  std::optional<Util::FunctionRef<bool(std::span<const uint8_t>)>> firstChunkCheck,
  GzipInflater* inflater
)
{
  OpenShock::SHA256 sha256;
  if (!sha256.begin()) {
    OS_LOGE(TAG, "Failed to initialize SHA256 hash");
    return false;
  }

  std::size_t contentLength  = 0;  // of the body as sent: the compressed size for gzip
  std::size_t received       = 0;  // body bytes received, what progress is measured in
  std::size_t contentWritten = 0;  // bytes written to the partition
  std::size_t erasedTo       = 0;
  int64_t lastProgress       = 0;

  // Where the time goes (us), logged once at the end: whatever isn't flash, hashing, inflating or progress reporting
  // is the network (TLS reads and waiting for data), which can't overlap the flash work in this single-task loop.
  struct {
    int64_t erase, write, hash, progress;
  } spent = {};

  // Decided by the first byte of the body, when an inflater was handed in. Every test of it goes with kEnabled: the
  // compiler can't prove it stays false once the download callback has it by reference.
  bool gzip            = false;
  int64_t inflateSpent = 0;  // us, excluding the flash work done from inside the inflater

  // Whether the body may be gzip. Its headers arrive before its first byte, so they can't tell yet.
  const bool mayBeGzip = GzipInflater::kEnabled && inflater != nullptr && inflater->valid();

  auto sizeValidator = [partition, mayBeGzip, &contentLength, progressCallback, &lastProgress, &spent](std::size_t size) -> bool {
    // Only a plain body is refused on its size here. A compressed one may be larger than the image it decodes to,
    // and writeOut bounds what is written either way.
    if (!mayBeGzip && size > partition->size) {
      OS_LOGE(TAG, "Remote partition binary is too large");
      return false;
    }

    // Erased incrementally by writeOut, just ahead of the data
    contentLength = size;

    lastProgress = OpenShock::millis();
    int64_t t0   = OpenShock::micros();
    progressCallback(0, contentLength, 0.0f);
    spent.progress += OpenShock::micros() - t0;

    return true;
  };

  // Writes the next piece of the image (decompressed, for gzip) to the partition and hashes it
  auto writeOut = [partition, &sha256, &contentWritten, &erasedTo, &firstChunkCheck, &spent](std::span<const uint8_t> chunk) -> bool {
    // Before the first erase: a refused first chunk leaves the partition as it was
    if (contentWritten == 0 && firstChunkCheck && !(*firstChunkCheck)(chunk)) {
      OS_LOGE(TAG, "The download is not what the partition takes; nothing was erased");
      return false;
    }

    // Content-Length only bounds a plain body; a decompressed one is bounded here
    if (chunk.size() > partition->size - contentWritten) {
      OS_LOGE(TAG, "The image is larger than the partition");
      return false;
    }

    int64_t t0 = OpenShock::micros();
    while (erasedTo < contentWritten + chunk.size()) {
      // The step is a multiple of the erase size, and partitions end on an erase-size boundary
      std::size_t eraseLength = std::min<std::size_t>(kEraseStep, partition->size - erasedTo);
      if (eraseLength == 0 || esp_partition_erase_range(partition, erasedTo, eraseLength) != ESP_OK) {
        OS_LOGE(TAG, "Failed to erase partition at offset %zu", erasedTo);
        return false;
      }
      erasedTo += eraseLength;
    }
    int64_t t1 = OpenShock::micros();

    if (esp_partition_write(partition, contentWritten, chunk.data(), chunk.size()) != ESP_OK) {
      OS_LOGE(TAG, "Failed to write to partition");
      return false;
    }
    int64_t t2 = OpenShock::micros();

    if (!sha256.update(chunk.data(), chunk.size())) {
      OS_LOGE(TAG, "Failed to update SHA256 hash");
      return false;
    }
    int64_t t3 = OpenShock::micros();

    contentWritten += chunk.size();

    spent.erase += t1 - t0;
    spent.write += t2 - t1;
    spent.hash += t3 - t2;

    return true;
  };

  auto dataWriter = [&](std::size_t offset, const uint8_t* data, std::size_t length) -> bool {
    if (length == 0) {
      return true;
    }

    received += length;

    if constexpr (GzipInflater::kEnabled) {
      if (offset == 0 && inflater != nullptr && inflater->valid()) {
        if (GzipInflater::StartsLikeGzip(data[0])) {
          if (!inflater->reset()) {
            return false;
          }
          gzip = true;
        } else {
          // Asked for gzip, got something else (e.g. a CDN that decoded it): flash it as is, the hash still decides
          OS_LOGW(TAG, "The body is not gzip, flashing it as is");
          inflater->release();
        }
      }
    }

    if (GzipInflater::kEnabled && gzip) {
      int64_t t0        = OpenShock::micros();
      int64_t sinkSpent = spent.erase + spent.write + spent.hash;
      if (inflater->feed(std::span<const uint8_t>(data, length), writeOut) == GzipInflater::Result::Error) {
        OS_LOGE(TAG, "Failed to decompress the download");
        return false;
      }
      inflateSpent += (OpenShock::micros() - t0) - (spent.erase + spent.write + spent.hash - sinkSpent);
    } else if (!writeOut(std::span<const uint8_t>(data, length))) {  // written in order, at contentWritten
      return false;
    }

    int64_t now = OpenShock::millis();
    if (now - lastProgress >= 500) {  // Send progress every 500ms
      lastProgress = now;
      int64_t t0   = OpenShock::micros();
      // A chunked body has no Content-Length to measure against
      progressCallback(received, contentLength, contentLength > 0 ? static_cast<float>(received) / static_cast<float>(contentLength) : 0.0f);
      spent.progress += OpenShock::micros() - t0;
    }

    return true;
  };

  int64_t downloadStart = OpenShock::micros();

  // Start streaming binary to app partition.
  auto appBinaryResponse = client.Download(
    remoteUrl,
    {
      {"Accept", OpenShock::HTTP::ContentType::OctetStream}
  },
    sizeValidator,
    dataWriter,
    std::array<uint16_t, 1> {200},
    180'000
  );  // 3 minutes
  if (appBinaryResponse.result != OpenShock::HTTP::RequestResult::Success) {
    OS_LOGE(TAG, "Failed to download remote partition binary: [%d]", appBinaryResponse.code);
    return false;
  }

  int64_t total = OpenShock::micros() - downloadStart;
  int64_t other = total - spent.erase - spent.write - spent.hash - spent.progress;

  if (GzipInflater::kEnabled && gzip) {
    if (!inflater->finished()) {
      OS_LOGE(TAG, "The gzip stream ended early");
      return false;
    }
    other -= inflateSpent;
    OS_LOGI(TAG, "Downloaded %zu bytes of gzip, inflating took %lld ms", received, inflateSpent / 1000);
  }

  OS_LOGI(
    TAG,
    "Flashed %zu bytes in %lld ms (%lld KB/s): erase %lld ms, write %lld ms, hash %lld ms, progress %lld ms, network+other %lld ms",
    contentWritten,
    total / 1000,
    total > 0 ? static_cast<int64_t>(contentWritten) * 1000 / total : 0,  // bytes/us * 1000 = KB/s (1 KB = 1000 B)
    spent.erase / 1000,
    spent.write / 1000,
    spent.hash / 1000,
    spent.progress / 1000,
    other / 1000
  );

  progressCallback(contentLength, contentLength, 1.0f);
  OS_LOGD(TAG, "Wrote %zu bytes to partition", appBinaryResponse.data);

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
