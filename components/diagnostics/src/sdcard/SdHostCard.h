#pragma once

#include "sdcard/BlockDevice.h"

#include <sd_protocol_types.h>

#include <cstdint>

namespace OpenShock::Diagnostics::SdCard {
  /// @brief The SD card, through ESP-IDF's sdmmc protocol layer on the bus the build selects: the SDMMC host
  ///        (CONFIG_OPENSHOCK_DIAG_SDCARD_BUS_SDMMC) or SPI mode on SPI2 (CONFIG_OPENSHOCK_DIAG_SDCARD_BUS_SPI).
  ///        Raw sectors only; no filesystem. Owns its host while initialised. Not thread-safe.
  class SdHostCard final : public BlockDevice {
  public:
    SdHostCard() = default;
    ~SdHostCard() override;

    SdHostCard(const SdHostCard&)            = delete;
    SdHostCard& operator=(const SdHostCard&) = delete;

    /// @brief Brings up the host and the card. Can be called again after a failure, or to re-detect the card.
    bool Init();
    void Deinit();

    /// @brief Whether the build configured every pin its bus needs.
    static bool PinsConfigured();

    bool Ready() const { return m_sectors != 0; }

    uint32_t SectorCount() const override { return m_sectors; }
    bool ReadSector(uint32_t lba, uint8_t* out) override { return ReadSectors(lba, out, 1); }
    bool WriteSector(uint32_t lba, const uint8_t* data) override { return WriteSectors(lba, data, 1); }
    bool ReadSectors(uint32_t lba, uint8_t* out, uint32_t count) override;
    bool WriteSectors(uint32_t lba, const uint8_t* data, uint32_t count) override;

  private:
    bool initHost();
    void deinitHost();

    sdmmc_host_t m_host {};
    sdmmc_card_t m_card {};
    bool m_hostUp      = false;
    int m_spiDevice    = -1;  // SPI mode: the sdspi device handle
    uint32_t m_sectors = 0;
  };
}  // namespace OpenShock::Diagnostics::SdCard
