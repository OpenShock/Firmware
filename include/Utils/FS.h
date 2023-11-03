#pragma once

#include <cstdint>
#include <memory>

namespace OpenShock {
  class FileSystem {
  public:
    static std::shared_ptr<FileSystem> GetWWW();
    static std::shared_ptr<FileSystem> GetConfig();

    FileSystem(const char* partitionLabel, const char* basePath, bool formatIfMountFailed, bool readOnly);
    ~FileSystem();

    bool ok() const;

    bool exists(const char* path) const;
    bool canRead(const char* path) const;
    bool canWrite(const char* path) const;
    bool canReadAndWrite(const char* path) const;

    bool deleteFile(const char* path);

    bool format();
  private:
    bool _checkAccess(const char* path, int mode) const;

    char* m_partitionLabel;
    char* m_basePath;
    bool m_readOnly;
  };
}  // namespace OpenShock
