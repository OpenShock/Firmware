#pragma once

#include <cstddef>
#include <cstdint>

namespace OpenShock::Diagnostics::SdCard {
  inline constexpr std::size_t kSectorSize = 512;

  /// @brief 512-byte sector storage addressed by 32-bit LBA: the SD card on the device, memory in the host tests.
  class BlockDevice {
  public:
    virtual ~BlockDevice() = default;

    virtual uint32_t SectorCount() const                        = 0;
    virtual bool ReadSector(uint32_t lba, uint8_t* out)         = 0;
    virtual bool WriteSector(uint32_t lba, const uint8_t* data) = 0;

    /// @brief Consecutive sectors in one operation where the device supports it.
    virtual bool ReadSectors(uint32_t lba, uint8_t* out, uint32_t count)
    {
      for (uint32_t i = 0; i < count; i++) {
        if (!ReadSector(lba + i, out + i * kSectorSize)) {
          return false;
        }
      }
      return true;
    }
    virtual bool WriteSectors(uint32_t lba, const uint8_t* data, uint32_t count)
    {
      for (uint32_t i = 0; i < count; i++) {
        if (!WriteSector(lba + i, data + i * kSectorSize)) {
          return false;
        }
      }
      return true;
    }
  };
}  // namespace OpenShock::Diagnostics::SdCard
