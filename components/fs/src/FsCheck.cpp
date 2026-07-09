#include "fs/FsCheck.h"

#include "fs/LfsPartition.h"

using namespace OpenShock;

bool OpenShock::TestFilesystem(const esp_partition_t* partition, bool writable)
{
  if (partition == nullptr) {
    return false;
  }

  LfsPartition fs;
  if (!fs.mount(partition, writable, /*formatOnFail=*/false)) {
    return false;
  }

  fs.unmount();
  return true;
}

bool OpenShock::TestFilesystem(const char* partitionLabel, bool writable)
{
  const esp_partition_t* partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, partitionLabel);
  return TestFilesystem(partition, writable);
}
