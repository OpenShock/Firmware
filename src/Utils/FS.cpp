#include "Utils/FS.h"

#include <esp_littlefs.h>

esp_err_t OpenShock::FS::registerPartition(const char* partitionLabel, const char* basePath, bool formatIfMountFailed, bool readOnly, bool growOnMount) {
  esp_vfs_littlefs_conf_t conf = {
    .base_path = basePath,
    .partition_label = partitionLabel,
    .partition = nullptr,
    .format_if_mount_failed = formatIfMountFailed,
    .read_only = readOnly,
    .dont_mount = false,
    .grow_on_mount = growOnMount,
  };

  return esp_vfs_littlefs_register(&conf);
}

esp_err_t OpenShock::FS::unregisterPartition(const char* paritionLabel) {
  return esp_vfs_littlefs_unregister(paritionLabel);
}
