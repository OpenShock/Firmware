#include "www/WwwPartition.h"

const char* const TAG = "WwwPartition";

#include "Hashing.h"
#include "Logging.h"

using namespace OpenShock;

const esp_partition_t* Www::FindPartition()
{
  return esp_partition_find_first(kPartitionType, ESP_PARTITION_SUBTYPE_ANY, kPartitionLabel);
}

bool Www::Sha256(std::span<const std::span<const uint8_t>> parts, std::span<uint8_t, 32> digest)
{
  OpenShock::SHA256 sha;
  std::array<uint8_t, 32> out;
  if (!sha.begin()) {
    return false;
  }
  for (std::span<const uint8_t> part : parts) {
    if (!sha.update(part.data(), part.size())) {
      return false;
    }
  }
  if (!sha.finish(out)) {
    return false;
  }
  std::copy(out.begin(), out.end(), digest.begin());
  return true;
}

namespace {
  // Maps the whole partition for the duration of `fn`.
  template<typename Fn>
  bool withMapped(const esp_partition_t* partition, Fn&& fn)
  {
    if (partition == nullptr) {
      return false;
    }
    const void* ptr                    = nullptr;
    esp_partition_mmap_handle_t handle = 0;
    esp_err_t err                      = esp_partition_mmap(partition, 0, partition->size, ESP_PARTITION_MMAP_DATA, &ptr, &handle);
    if (err != ESP_OK) {
      OS_LOGE(TAG, "Failed to map %s: %s", partition->label, esp_err_to_name(err));
      return false;
    }
    bool ok = fn(std::span<const uint8_t>(static_cast<const uint8_t*>(ptr), partition->size));
    esp_partition_munmap(handle);
    return ok;
  }
}  // namespace

bool Www::VerifyPartition(const esp_partition_t* partition)
{
  return withMapped(partition, [](std::span<const uint8_t> bytes) {
    auto image = Image::Parse(bytes, Sha256);
    if (!image) {
      OS_LOGE(TAG, "Partition holds no valid www image: %s", ParseErrorName(image.error()));
      return false;
    }
    return true;
  });
}

bool Www::ImageSha256(const esp_partition_t* partition, std::array<uint8_t, 32>& digest)
{
  return withMapped(partition, [&](std::span<const uint8_t> bytes) {
    auto image = Image::Parse(bytes, Sha256);
    if (!image) {
      return false;
    }
    const std::span<const uint8_t> whole = image->Bytes();
    return Sha256(std::span(&whole, 1), digest);
  });
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

  auto image = Image::Parse(std::span<const uint8_t>(static_cast<const uint8_t*>(ptr), partition->size), Sha256);
  if (!image) {
    OS_LOGE(TAG, "%s holds no valid www image (%s); the portal has no assets until one is flashed", partition->label, ParseErrorName(image.error()));
    esp_partition_munmap(m_handle);
    m_handle = 0;
    return false;
  }

  m_image.emplace(*image);
  OS_LOGI(TAG, "Mapped %u assets (%u bytes) from %s", static_cast<unsigned>(m_image->Count()), static_cast<unsigned>(m_image->ImageSize()), partition->label);
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
