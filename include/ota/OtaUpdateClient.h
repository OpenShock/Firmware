#pragma once

#include "SemVer.h"

#include <freertos/task.h>

namespace OpenShock {
  class OtaUpdateClient {
  public:
    OtaUpdateClient(const OpenShock::SemVer& version);
    ~OtaUpdateClient();

    bool Start();
  private:
    void _task();

    OpenShock::SemVer m_version;
    TaskHandle_t m_taskHandle;
  };
}
