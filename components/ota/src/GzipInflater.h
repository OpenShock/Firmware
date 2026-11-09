#pragma once

#include "util/FunctionRef.h"

#include <sdkconfig.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace OpenShock {
  /// @brief Streaming gzip decoder: zlib's inflate, with all of its memory allocated up front.
  ///
  /// The stream must be compressed with a window of at most kWindowBits (gzip/zlib `wbits` <= 12, a 4 KB window);
  /// zlib rejects a stream that refers back further than that ("invalid distance too far back").
  ///
  /// Construction allocates one block that zlib's allocations are served from (its state and window) plus the
  /// output buffer, and keeps it only if CONFIG_OPENSHOCK_OTA_GZIP_MIN_FREE_BLOCK of contiguous internal RAM is left
  /// for the TLS connections that follow. So the caller learns whether the memory is there before it asks for a
  /// compressed download. release() gives it back early, e.g. when the server did not send gzip. The caller owns the
  /// lifetime; nothing is pooled.
  ///
  /// Without CONFIG_OPENSHOCK_OTA_GZIP it never allocates and is never valid. Guard the gzip paths with
  /// `if constexpr (GzipInflater::kEnabled)`: they stay compiled and type-checked, but emit no code, so nothing
  /// reaches zlib and the linker drops it.
  ///
  /// Not thread-safe.
  class GzipInflater {
  public:
    // The one #if: Kconfig leaves a disabled bool undefined, so it can't be used in `if constexpr` directly
#if CONFIG_OPENSHOCK_OTA_GZIP
    static constexpr bool kEnabled = true;
#else
    static constexpr bool kEnabled = false;
#endif

    static constexpr int kWindowBits         = 12;
    static constexpr std::size_t kOutputSize = 4096;  // the sink gets pieces of this size, the last one shorter

    /// @brief Receives decompressed output in order. Returning false aborts decoding.
    using Sink = Util::FunctionRef<bool(std::span<const uint8_t>)>;

    enum class Result : uint8_t {
      NeedMoreInput,  // all input consumed, the stream is not finished
      Done,           // the stream ended and zlib verified its CRC-32 and size
      Error,          // malformed stream, trailing data, or the sink refused
    };

    /// @brief Whether a body starting with this byte is gzip (the first magic byte). A firmware image starts with
    ///        0xE9, so one byte tells the two apart; a body that starts like gzip but is not fails in feed().
    [[nodiscard]] static constexpr bool StartsLikeGzip(uint8_t firstByte) noexcept { return firstByte == 0x1F; }

    GzipInflater(const GzipInflater&)            = delete;
    GzipInflater& operator=(const GzipInflater&) = delete;
    GzipInflater(GzipInflater&&)                 = delete;
    GzipInflater& operator=(GzipInflater&&)      = delete;

    GzipInflater() noexcept;
    ~GzipInflater();

    /// @brief Whether the memory is held, i.e. the constructor succeeded and release() was not called.
    [[nodiscard]] bool valid() const noexcept { return m_storage != nullptr; }

    /// @brief Frees the memory now instead of at destruction. The inflater is unusable afterwards.
    void release() noexcept;

    /// @brief Bytes held by a valid inflater.
    [[nodiscard]] static std::size_t AllocationSize() noexcept;

    /// @brief Starts a new stream. Must be called before the first feed() of every stream.
    [[nodiscard]] bool reset() noexcept;

    /// @brief Decodes the next piece of the compressed stream. After Error every call returns Error, and after Done
    ///        any further input does, until reset().
    [[nodiscard]] Result feed(std::span<const uint8_t> input, Sink sink) noexcept;

    /// @brief Whether the stream has ended and verified.
    [[nodiscard]] bool finished() const noexcept { return m_done; }

  private:
    struct Storage;

    void allocate() noexcept;

    static void* zalloc(void* opaque, unsigned items, unsigned size);
    static void zfree(void* opaque, void* address);

    Storage* m_storage;
    bool m_done;
    bool m_failed;
  };
}  // namespace OpenShock
