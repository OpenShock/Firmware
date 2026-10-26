#include "Coredump.h"

const char* const TAG = "Coredump";

#include "Checksum.h"
#include "Logging.h"

#include <esp_core_dump.h>
#include <esp_flash.h>

#include <algorithm>
#include <cstring>
#include <span>

using namespace OpenShock::Diagnostics;

std::optional<Coredump::Image> Coredump::Find()
{
  Image image {};
  esp_err_t err = esp_core_dump_image_get(&image.address, &image.size);
  if (err != ESP_OK) {
    return std::nullopt;  // ESP_ERR_NOT_FOUND / ESP_ERR_INVALID_SIZE: nothing stored
  }

  err = esp_core_dump_image_check();
  if (err != ESP_OK) {
    OS_LOGW(TAG, "Stored crash dump is damaged (%s), erasing it", esp_err_to_name(err));
    Erase();
    return std::nullopt;
  }

  return image;
}

bool Coredump::Read(const Image& image, uint32_t offset, uint8_t* out, std::size_t len)
{
  if (offset > image.size || len > image.size - offset) {
    return false;
  }
  return esp_flash_read(nullptr, out, image.address + offset, len) == ESP_OK;
}

bool Coredump::Erase()
{
  esp_err_t err = esp_core_dump_image_erase();
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to erase the crash dump: %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

std::string Coredump::PanicReason()
{
  char buffer[128] = {};
  if (esp_core_dump_get_panic_reason(buffer, sizeof(buffer)) != ESP_OK) {
    return {};
  }

  std::string reason;
  for (char c : buffer) {
    if (c == '\0') {
      break;
    }
    reason.push_back(c >= 0x20 && c < 0x7F ? c : ' ');
  }
  return reason;
}

std::optional<uint32_t> Coredump::Crc(const Image& image)
{
  uint8_t chunk[512];
  uint32_t crc = 0;
  for (std::size_t offset = 0; offset < image.size; offset += sizeof(chunk)) {
    std::size_t len = std::min(sizeof(chunk), image.size - offset);
    if (!Read(image, static_cast<uint32_t>(offset), chunk, len)) {
      return std::nullopt;
    }
    crc = OpenShock::Checksum::Crc32(std::span<const uint8_t>(chunk, len), crc);
  }
  return crc;
}

std::optional<Coredump::Summary> Coredump::GetSummary()
{
  esp_core_dump_summary_t summary = {};
  if (esp_core_dump_get_summary(&summary) != ESP_OK) {
    return std::nullopt;
  }

  Summary result {};
  for (uint8_t c : summary.app_elf_sha256) {
    if (c == '\0') {
      break;
    }
    result.elfSha256.push_back(static_cast<char>(c));
  }
  result.excPc = summary.exc_pc;
  std::memcpy(result.excTask, summary.exc_task, sizeof(result.excTask));
  result.excTask[sizeof(result.excTask) - 1] = '\0';
  return result;
}
