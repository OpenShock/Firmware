#pragma once

// The `www` partition on the device: found by its custom type (0x40, so no ESP-IDF component ever tries to
// mount it), mapped once through the flash cache and parsed with WwwImage.h. The portal serves straight
// from the mapped pointer.

#include "OpenShock.h"
#include "www/WwwImage.h"

#include <esp_partition.h>

#include <array>
#include <cstdint>
#include <optional>

namespace OpenShock::Www {
  inline constexpr esp_partition_type_t kPartitionType       = static_cast<esp_partition_type_t>(0x40);
  inline constexpr esp_partition_subtype_t kPartitionSubtype = static_cast<esp_partition_subtype_t>(0x00);
  inline constexpr const char* kPartitionLabel                = "www";

  /// @brief The `www` partition of the running partition table, or null on a table without one (the 1.x layout).
  [[nodiscard]] const esp_partition_t* FindPartition();

  /// @brief SHA-256 over `parts`, concatenated: the hasher WwwImage expects (the crypto component's SHA256).
  bool Sha256(std::span<const std::span<const uint8_t>> parts, std::span<uint8_t, 32> digest);

  /// @brief Whether `partition` holds a valid image; what the OTA path runs on a freshly flashed partition.
  [[nodiscard]] bool VerifyPartition(const esp_partition_t* partition);

  /// @brief SHA-256 of the image in `partition` (header to the end of the last body, not the whole partition), so
  ///        it can be compared with the hash the repository server publishes for www.bin. False if the image is invalid.
  [[nodiscard]] bool ImageSha256(const esp_partition_t* partition, std::array<uint8_t, 32>& digest);

  // Keeps the partition mapped for as long as the object lives. One per captive-portal instance.
  class Partition {
    DISABLE_COPY(Partition);
    DISABLE_MOVE(Partition);

  public:
    Partition() = default;
    ~Partition() { unmount(); }

    /// @brief Maps and validates the image. Logs why when it fails; the portal then reports "no assets".
    bool mount(const esp_partition_t* partition);
    void unmount();
    [[nodiscard]] bool isMounted() const { return m_image.has_value(); }

    [[nodiscard]] const Image& image() const { return *m_image; }
    [[nodiscard]] std::optional<Asset> find(std::string_view path) const { return m_image ? m_image->Find(path) : std::nullopt; }

  private:
    std::optional<Image> m_image;
    esp_partition_mmap_handle_t m_handle = 0;
  };
}  // namespace OpenShock::Www
