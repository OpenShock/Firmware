#pragma once

#include <esp_err.h>

#include <cstdint>

namespace OpenShock::FS {
  esp_err_t registerPartition(const char* partitionLabel, const char* basePath, bool formatIfMountFailed, bool readOnly, bool growOnMount);
  esp_err_t unregisterPartition(const char* partitionLabel);
}
