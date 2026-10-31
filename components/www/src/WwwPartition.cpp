#include "www/WwwPartition.h"

const char* const TAG = "WwwPartition";

#include "Hashing.h"
#include "Logging.h"

#include <span>

using namespace OpenShock;

const esp_partition_t* Www::FindPartition()
{
  return esp_partition_find_first(kPartitionType, kPartitionSubtype, kPartitionLabel);
}

bool Www::VerifyPartition(const esp_partition_t* partition)
{
  // Mapped for just this call; mount() logs why when the image is not valid
  Partition mapped;
  return mapped.mount(partition);
}

bool Www::ImageSha256(const esp_partition_t* partition, std::array<uint8_t, 32>& digest)
{
  Partition mapped;
  if (!mapped.mount(partition)) {
    return false;
  }
  const std::span<const uint8_t> whole = mapped.image().Bytes();
  return SHA256::compute(whole.data(), whole.size(), digest);
}

bool Www::Partition::mount(const esp_partition_t* partition)
{
  unmount();
  if (partition == nullptr) {
    OS_LOGE(TAG, "No www partition");
    return false;
  }

  const void* ptr = nullptr;
  esp_err_t err   = esp_partition_mmap(partition, 0, partition->size, ESP_PARTITION_MMAP_DATA, &ptr, &m_handle);
  if (err != ESP_OK) {
    OS_LOGE(TAG, "Failed to map %s: %s", partition->label, esp_err_to_name(err));
    m_handle = 0;
    return false;
  }

  auto image = Image::Parse(std::span<const uint8_t>(static_cast<const uint8_t*>(ptr), partition->size));
  if (!image) {
    OS_LOGE(TAG, "%s holds no valid www image (%s)", partition->label, ParseErrorName(image.error()));
    esp_partition_munmap(m_handle);
    m_handle = 0;
    return false;
  }

  m_image.emplace(*image);
  OS_LOGI(TAG, "Mapped www image from %s: %u nodes, %u bytes", partition->label, static_cast<unsigned>(m_image->Count()), static_cast<unsigned>(m_image->ImageSize()));
  return true;
}

void Www::Partition::unmount()
{
  m_image.reset();
  if (m_handle != 0) {
    esp_partition_munmap(m_handle);
    m_handle = 0;
  }
}
