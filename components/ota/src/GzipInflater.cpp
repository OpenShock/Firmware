#include <freertos/FreeRTOS.h>

#include "GzipInflater.h"

const char* const TAG = "GzipInflater";

#include "Logging.h"

#include <esp_heap_caps.h>
#include <zlib.h>

#include <new>

using namespace OpenShock;

// zlib's inflate allocates its state (struct inflate_state) at init and its window on the first output. Both come
// out of the arena, so the arena must hold both: the window exactly, the state with some headroom since its size is
// internal to zlib. The constructor checks the window still fits after init, so a short arena fails up front rather
// than mid-download.
static constexpr std::size_t kWindowSize     = std::size_t {1} << GzipInflater::kWindowBits;
static constexpr std::size_t kStateReserve   = 8 * 1024;
static constexpr std::size_t kArenaSize      = kStateReserve + kWindowSize;
static constexpr std::size_t kArenaAlignment = alignof(std::max_align_t);

struct GzipInflater::Storage {
  z_stream stream;
  std::size_t arenaUsed;
  std::size_t outputUsed;
  uint8_t output[kOutputSize];
  alignas(kArenaAlignment) uint8_t arena[kArenaSize];
};

// Bump allocation out of the arena; zfree is a no-op and the whole arena goes back in release().
void* GzipInflater::zalloc(void* opaque, unsigned items, unsigned size)
{
  auto* storage = static_cast<Storage*>(opaque);

  if (size != 0 && items > SIZE_MAX / size) {
    return Z_NULL;
  }
  std::size_t length = static_cast<std::size_t>(items) * size;
  std::size_t start  = (storage->arenaUsed + kArenaAlignment - 1) & ~(kArenaAlignment - 1);
  if (start > kArenaSize || length > kArenaSize - start) {
    OS_LOGE(TAG, "zlib asked for %zu bytes, %zu of %zu left", length, start > kArenaSize ? 0 : kArenaSize - start, kArenaSize);
    return Z_NULL;
  }

  storage->arenaUsed = start + length;
  return storage->arena + start;
}

void GzipInflater::zfree(void* /*opaque*/, void* /*address*/)
{
}

GzipInflater::GzipInflater() noexcept
  : m_storage(nullptr)
  , m_done(false)
  , m_failed(true)
{
  if constexpr (kEnabled) {
    allocate();
  }
}

void GzipInflater::allocate() noexcept
{
  void* memory = heap_caps_malloc(sizeof(Storage), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (memory == nullptr) {
    OS_LOGW(TAG, "Failed to allocate %zu bytes for the gzip decoder", sizeof(Storage));
    return;
  }

  Storage* storage       = new (memory) Storage;
  storage->arenaUsed     = 0;
  storage->outputUsed    = 0;
  storage->stream        = {};
  storage->stream.zalloc = &GzipInflater::zalloc;
  storage->stream.zfree  = &GzipInflater::zfree;
  storage->stream.opaque = storage;

  // 16 + wbits: a gzip wrapper (header, CRC-32 and size trailer), no zlib or raw stream
  int ret = inflateInit2(&storage->stream, 16 + kWindowBits);
  if (ret != Z_OK) {
    OS_LOGE(TAG, "inflateInit2 failed (%d)", ret);
    storage->~Storage();
    heap_caps_free(memory);
    return;
  }

  // The window is allocated on the first output; make sure it will fit
  std::size_t windowStart = (storage->arenaUsed + kArenaAlignment - 1) & ~(kArenaAlignment - 1);
  if (windowStart > kArenaSize || kArenaSize - windowStart < kWindowSize) {
    OS_LOGE(TAG, "zlib's state took %zu bytes, no room left for its %zu byte window", storage->arenaUsed, kWindowSize);
    inflateEnd(&storage->stream);
    storage->~Storage();
    heap_caps_free(memory);
    return;
  }

  m_storage = storage;
  m_failed  = false;

  // Only worth holding if the TLS connections that follow still fit
  std::size_t largestFree = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (largestFree < CONFIG_OPENSHOCK_OTA_GZIP_MIN_FREE_BLOCK) {
    OS_LOGW(TAG, "Only %zu bytes contiguous left after the gzip decoder, not keeping it", largestFree);
    release();
    return;
  }

  OS_LOGI(TAG, "Allocated %zu bytes for the gzip decoder, %zu bytes contiguous left", sizeof(Storage), largestFree);
}

GzipInflater::~GzipInflater()
{
  release();
}

void GzipInflater::release() noexcept
{
  // Discarded without gzip (it never allocated), which keeps inflateEnd out of the link
  if constexpr (kEnabled) {
    if (m_storage == nullptr) {
      return;
    }
    inflateEnd(&m_storage->stream);
    m_storage->~Storage();
    heap_caps_free(m_storage);
    m_storage = nullptr;
    m_done    = false;
    m_failed  = true;
  }
}

std::size_t GzipInflater::AllocationSize() noexcept
{
  return sizeof(Storage);
}

bool GzipInflater::reset() noexcept
{
  if (m_storage == nullptr) {
    return false;
  }

  // Keeps the state and window zlib already allocated
  int ret = inflateReset(&m_storage->stream);
  if (ret != Z_OK) {
    OS_LOGE(TAG, "inflateReset failed (%d)", ret);
    m_failed = true;
    return false;
  }

  m_storage->outputUsed = 0;
  m_done                = false;
  m_failed              = false;
  return true;
}

GzipInflater::Result GzipInflater::feed(std::span<const uint8_t> input, Sink sink) noexcept
{
  if (m_storage == nullptr || m_failed) {
    return Result::Error;
  }

  if (m_done) {
    if (input.empty()) {
      return Result::Done;
    }
    // A second member (concatenated gzip) or junk; neither is something the release tooling produces
    OS_LOGE(TAG, "Data after the end of the gzip stream");
    m_failed = true;
    return Result::Error;
  }

  z_stream& stream = m_storage->stream;
  stream.next_in   = const_cast<Bytef*>(input.data());  // zlib's API is not const-correct; it only reads the input
  stream.avail_in  = static_cast<uInt>(input.size());

  while (true) {
    std::size_t& outputUsed = m_storage->outputUsed;
    stream.next_out         = m_storage->output + outputUsed;
    stream.avail_out        = static_cast<uInt>(kOutputSize - outputUsed);

    int ret    = inflate(&stream, Z_NO_FLUSH);
    outputUsed = kOutputSize - stream.avail_out;

    // Z_BUF_ERROR only means no progress was possible this call, checked below
    if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
      OS_LOGE(TAG, "Failed to decompress (%d: %s)", ret, stream.msg != nullptr ? stream.msg : "no message");
      m_failed = true;
      return Result::Error;
    }

    bool outputFull = stream.avail_out == 0;
    if (outputFull || (ret == Z_STREAM_END && outputUsed > 0)) {
      if (!sink(std::span<const uint8_t>(m_storage->output, outputUsed))) {
        m_failed = true;
        return Result::Error;
      }
      outputUsed = 0;
    }

    if (ret == Z_STREAM_END) {
      if (stream.avail_in != 0) {
        OS_LOGE(TAG, "Data after the end of the gzip stream");
        m_failed = true;
        return Result::Error;
      }
      m_done = true;
      return Result::Done;
    }

    // A full buffer may have left output pending in zlib, so go round again even when the input is used up
    if (!outputFull && stream.avail_in == 0) {
      return Result::NeedMoreInput;
    }

    if (ret == Z_BUF_ERROR && !outputFull) {
      // Room for output and input left, yet no progress: not a state a well-formed stream gets zlib into
      OS_LOGE(TAG, "zlib made no progress");
      m_failed = true;
      return Result::Error;
    }
  }
}
