#include "Utils/FS.h"

#include "Logging.h"

#include <esp32-hal.h>
#include <esp_littlefs.h>

#include <cstring>

const char* const TAG = "FS";

using namespace OpenShock;

std::shared_ptr<FileSystem> FileSystem::GetWWW() {
  static std::weak_ptr<FileSystem> s_fileSystem;

  std::shared_ptr<FileSystem> fileSystem = s_fileSystem.lock();

  if (!fileSystem) {
    fileSystem   = std::make_shared<FileSystem>("www", "/www", false, true);
    s_fileSystem = fileSystem;
  }

  return fileSystem;
}

std::shared_ptr<FileSystem> FileSystem::GetConfig() {
  static std::weak_ptr<FileSystem> s_fileSystem;

  std::shared_ptr<FileSystem> fileSystem = s_fileSystem.lock();

  if (!fileSystem) {
    fileSystem   = std::make_shared<FileSystem>("config", "/config", true, false);
    s_fileSystem = fileSystem;
  }

  return fileSystem;
}

FileSystem::FileSystem(const char* partitionLabel, const char* basePath, bool formatIfMountFailed, bool readOnly) : m_partitionLabel(nullptr), m_basePath(nullptr), m_readOnly(readOnly) {
  if (partitionLabel == nullptr || basePath == nullptr) {
    ESP_LOGE(TAG, "An Error has occurred while mounting LittleFS: partitionLabel or basePath is null");
    return;
  }

  if (esp_littlefs_mounted(partitionLabel)) {
    ESP_LOGE(TAG, "An Error has occurred while mounting LittleFS (%s): already mounted", partitionLabel);
    return;
  }

  m_partitionLabel = strdup(partitionLabel);
  m_basePath       = strdup(basePath);

  esp_vfs_littlefs_conf_t conf = {
    .base_path              = basePath,
    .partition_label        = partitionLabel,
    .format_if_mount_failed = formatIfMountFailed,
    .read_only              = readOnly,
    .dont_mount             = false,
  };

  esp_err_t err = esp_vfs_littlefs_register(&conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "An Error has occurred while mounting LittleFS (%s): %s", partitionLabel, esp_err_to_name(err));

    free(m_partitionLabel);
    m_partitionLabel = nullptr;

    free(m_basePath);
    m_basePath       = nullptr;

    return;
  }
}

FileSystem::~FileSystem() {
  if (m_partitionLabel && esp_littlefs_mounted(m_partitionLabel)) {
    esp_err_t err = esp_vfs_littlefs_unregister(m_partitionLabel);
    if (err) {
      ESP_LOGE(TAG, "An Error has occurred while unmounting LittleFS (%s): %s", m_partitionLabel, esp_err_to_name(err));
    }
  }
  free(m_partitionLabel);
  free(m_basePath);
}

bool FileSystem::ok() const {
  return m_partitionLabel != nullptr && m_basePath != nullptr;
}

bool FileSystem::exists(const char* path) const {
  return _checkAccess(path, F_OK);
}

bool FileSystem::canRead(const char* path) const {
  return _checkAccess(path, R_OK);
}

bool FileSystem::canWrite(const char* path) const {
  return _checkAccess(path, W_OK);
}

bool FileSystem::canReadAndWrite(const char* path) const {
  return _checkAccess(path, R_OK | W_OK);
}

bool FileSystem::deleteFile(const char* path) {
  if (!ok() || m_readOnly) {
    return false;
  }

  char fullPath[256];
  strcpy(fullPath, m_basePath);
  if (path[0] != '/') {
    strcat(fullPath, "/");
  }
  strcat(fullPath, path);

  return unlink(fullPath) == 0;
}

bool FileSystem::_checkAccess(const char* path, int mode) const {
  if (!ok()) {
    return false;
  }

  char fullPath[256];
  strcpy(fullPath, m_basePath);
  if (path[0] != '/') {
    strcat(fullPath, "/");
  }
  strcat(fullPath, path);

  return access(fullPath, mode) == 0;
}

bool FileSystem::format() {
  if (!ok() || m_readOnly) {
    return false;
  }

  disableCore0WDT();
  esp_err_t err = esp_littlefs_format(m_partitionLabel);
  enableCore0WDT();

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "An Error has occurred while formatting LittleFS (%s): %s", m_partitionLabel, esp_err_to_name(err));
    return false;
  }

  return true;
}
