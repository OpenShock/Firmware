#pragma once

namespace OpenShock::OtaUpdateManager {

  enum OtaUpdateState {
    /* Nothing is happening. */
    NONE,

    /* Scheduled or manual version check found a new version is avaiable. */
    UPDATE_AVAILABLE,

    /* Currently downloading new firmware to other OTA slot. */
    FIRMWARE_DOWNLOADING,

    /* New firmware is ready to boot. */
    FIRMWARE_READY,

    /* Waiting for WiFi to begin downloading new filesystem. */
    FILESYSTEM_PENDING_WIFI,

    /* Filesystem is being downloaded. */
    FILESYSTEM_UPDATING,
  };

  void Init();
  void Update();

  bool IsUpdateAvailable();
  bool IsPerformingUpdate();

  OtaUpdateState GetState();

}  // namespace OpenShock::OtaUpdateManager
