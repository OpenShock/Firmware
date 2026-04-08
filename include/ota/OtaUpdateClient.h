#pragma once

#include "ota/FirmwareReleaseInfo.h"
#include "SemVer.h"

#include <freertos/task.h>

namespace OpenShock {
  class OtaUpdateClient {
  public:
    OtaUpdateClient(const OpenShock::SemVer& version, const FirmwareReleaseInfo& release);
    ~OtaUpdateClient();

    bool Start();

  private:
    void task();

    OpenShock::SemVer m_version;
    FirmwareReleaseInfo m_release;
    TaskHandle_t m_taskHandle;
  };
}  // namespace OpenShock
