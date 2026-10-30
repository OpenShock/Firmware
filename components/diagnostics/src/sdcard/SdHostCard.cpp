#include <freertos/FreeRTOS.h>

#include "sdcard/SdHostCard.h"

const char* const TAG = "SdHostCard";

#include "Logging.h"

#include <sdmmc_cmd.h>

#if CONFIG_OPENSHOCK_DIAG_SDCARD_BUS_SDMMC
#include <driver/sdmmc_host.h>
#else
#include <driver/sdspi_host.h>
#include <driver/spi_master.h>
#endif

using namespace OpenShock::Diagnostics::SdCard;

#if CONFIG_OPENSHOCK_DIAG_SDCARD_BUS_SPI
static constexpr spi_host_device_t kSpiHost = SPI2_HOST;
#endif

SdHostCard::~SdHostCard()
{
  Deinit();
}

bool SdHostCard::PinsConfigured()
{
#if CONFIG_OPENSHOCK_DIAG_SDCARD_BUS_SDMMC
  // D1-D3 are optional: without D3 the bus runs 1-bit, and 4-bit needs all three.
  bool data = CONFIG_OPENSHOCK_DIAG_SDCARD_D3_GPIO < 0 || (CONFIG_OPENSHOCK_DIAG_SDCARD_D1_GPIO >= 0 && CONFIG_OPENSHOCK_DIAG_SDCARD_D2_GPIO >= 0);
  return CONFIG_OPENSHOCK_DIAG_SDCARD_CLK_GPIO >= 0 && CONFIG_OPENSHOCK_DIAG_SDCARD_CMD_GPIO >= 0 && CONFIG_OPENSHOCK_DIAG_SDCARD_D0_GPIO >= 0 && data;
#else
  return CONFIG_OPENSHOCK_DIAG_SDCARD_SCK_GPIO >= 0 && CONFIG_OPENSHOCK_DIAG_SDCARD_MOSI_GPIO >= 0 && CONFIG_OPENSHOCK_DIAG_SDCARD_MISO_GPIO >= 0 && CONFIG_OPENSHOCK_DIAG_SDCARD_CS_GPIO >= 0;
#endif
}

bool SdHostCard::initHost()
{
#if CONFIG_OPENSHOCK_DIAG_SDCARD_BUS_SDMMC
  m_host = SDMMC_HOST_DEFAULT();
  m_host.flags &= ~SDMMC_HOST_FLAG_DDR;
  m_host.max_freq_khz = CONFIG_OPENSHOCK_DIAG_SDCARD_FREQ_KHZ;

  esp_err_t err = sdmmc_host_init();
  if (err != ESP_OK) {
    OS_LOGE(TAG, "SDMMC host init failed: %s", esp_err_to_name(err));
    return false;
  }
  m_hostUp = true;

  // Card detect stays with SdStorage (debounced, and polled while no card is in), so the slot gets none.
  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.clk                 = static_cast<gpio_num_t>(CONFIG_OPENSHOCK_DIAG_SDCARD_CLK_GPIO);
  slot.cmd                 = static_cast<gpio_num_t>(CONFIG_OPENSHOCK_DIAG_SDCARD_CMD_GPIO);
  slot.d0                  = static_cast<gpio_num_t>(CONFIG_OPENSHOCK_DIAG_SDCARD_D0_GPIO);
  slot.d1                  = static_cast<gpio_num_t>(CONFIG_OPENSHOCK_DIAG_SDCARD_D1_GPIO);
  slot.d2                  = static_cast<gpio_num_t>(CONFIG_OPENSHOCK_DIAG_SDCARD_D2_GPIO);
  slot.d3                  = static_cast<gpio_num_t>(CONFIG_OPENSHOCK_DIAG_SDCARD_D3_GPIO);
  slot.cd                  = SDMMC_SLOT_NO_CD;
  slot.wp                  = SDMMC_SLOT_NO_WP;
  slot.width               = CONFIG_OPENSHOCK_DIAG_SDCARD_D3_GPIO >= 0 ? 4 : 1;
  slot.flags               = 0;

  err = sdmmc_host_init_slot(m_host.slot, &slot);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "SDMMC slot init failed: %s", esp_err_to_name(err));
    return false;
  }
  return true;
#else
  spi_bus_config_t bus {};
  bus.mosi_io_num     = CONFIG_OPENSHOCK_DIAG_SDCARD_MOSI_GPIO;
  bus.miso_io_num     = CONFIG_OPENSHOCK_DIAG_SDCARD_MISO_GPIO;
  bus.sclk_io_num     = CONFIG_OPENSHOCK_DIAG_SDCARD_SCK_GPIO;
  bus.quadwp_io_num   = -1;
  bus.quadhd_io_num   = -1;
  bus.max_transfer_sz = 4096;

  esp_err_t err = spi_bus_initialize(kSpiHost, &bus, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
    return false;
  }
  m_hostUp = true;

  err = sdspi_host_init();
  if (err != ESP_OK) {
    OS_LOGE(TAG, "SDSPI host init failed: %s", esp_err_to_name(err));
    return false;
  }

  sdspi_device_config_t device = SDSPI_DEVICE_CONFIG_DEFAULT();
  device.host_id               = kSpiHost;
  device.gpio_cs               = static_cast<gpio_num_t>(CONFIG_OPENSHOCK_DIAG_SDCARD_CS_GPIO);
  sdspi_dev_handle_t handle;
  err = sdspi_host_init_device(&device, &handle);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "SDSPI device init failed: %s", esp_err_to_name(err));
    return false;
  }
  m_spiDevice = handle;

  m_host              = SDSPI_HOST_DEFAULT();
  m_host.slot         = handle;
  m_host.max_freq_khz = CONFIG_OPENSHOCK_DIAG_SDCARD_FREQ_KHZ;
  return true;
#endif
}

void SdHostCard::deinitHost()
{
#if CONFIG_OPENSHOCK_DIAG_SDCARD_BUS_SDMMC
  if (m_hostUp) {
    sdmmc_host_deinit();
  }
#else
  if (m_spiDevice >= 0) {
    sdspi_host_remove_device(m_spiDevice);
    m_spiDevice = -1;
  }
  if (m_hostUp) {
    sdspi_host_deinit();
    spi_bus_free(kSpiHost);
  }
#endif
  m_hostUp = false;
}

bool SdHostCard::Init()
{
  Deinit();
  if (!initHost()) {
    deinitHost();
    return false;
  }

  esp_err_t err = sdmmc_card_init(&m_host, &m_card);
  if (err != ESP_OK) {
    OS_LOGW(TAG, "SD card init failed: %s", esp_err_to_name(err));
    deinitHost();
    return false;
  }
  if (m_card.csd.sector_size != kSectorSize) {
    OS_LOGE(TAG, "SD card has %d-byte sectors, not %u", m_card.csd.sector_size, static_cast<unsigned>(kSectorSize));
    deinitHost();
    return false;
  }

  m_sectors = static_cast<uint32_t>(m_card.csd.capacity);
  OS_LOGI(TAG, "SD card: %s, %lu MiB, %d kHz", m_card.cid.name, static_cast<unsigned long>(m_sectors / 2048), m_card.real_freq_khz);
  return true;
}

void SdHostCard::Deinit()
{
  m_sectors = 0;
  deinitHost();
}

bool SdHostCard::ReadSectors(uint32_t lba, uint8_t* out, uint32_t count)
{
  if (!Ready() || lba + count > m_sectors || lba + count < lba) {
    return false;
  }
  esp_err_t err = sdmmc_read_sectors(&m_card, out, lba, count);
  if (err != ESP_OK) {
    OS_LOGW(TAG, "Read of %lu sectors at %lu failed: %s", static_cast<unsigned long>(count), static_cast<unsigned long>(lba), esp_err_to_name(err));
    return false;
  }
  return true;
}

bool SdHostCard::WriteSectors(uint32_t lba, const uint8_t* data, uint32_t count)
{
  if (!Ready() || lba + count > m_sectors || lba + count < lba) {
    return false;
  }
  esp_err_t err = sdmmc_write_sectors(&m_card, data, lba, count);
  if (err != ESP_OK) {
    OS_LOGW(TAG, "Write of %lu sectors at %lu failed: %s", static_cast<unsigned long>(count), static_cast<unsigned long>(lba), esp_err_to_name(err));
    return false;
  }
  return true;
}
