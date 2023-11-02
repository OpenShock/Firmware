#pragma once

bool verifyRollbackLater();

namespace OpenShock::OtaUpdateManager {

  enum class BootMode {
    NORMAL,
    OTA_UPDATE,
  };

  enum class Preparation {
    /* To our knowledge, no other firmware version is present on the device. */
    NONE,

    /* We are in the process of downloading and flashing new firmware to an unused OTA slot. */
    IMAGE_FLASHING,

    /* New firmware is pending in another OTA slot. */
    IMAGE_FLASHED,
  };

  enum class UpdateState {
    NONE,

    /* Waiting for WiFi to begin downloading new filesystem. */
    PENDING_WIFI,

    /* Filesystem is being downloaded. */
    FLASHING,

    /* Something failed. */
    ERROR,
  };

  /* Initialize the OtaUpdateManager. */
  void Init();

  /* Alternative setup() method. */
  void Setup();
  /* Alternative loop() method. */
  void Loop();

  bool LoadConfig();
  bool SaveConfig();

  bool IsPerformingUpdate();

  BootMode GetBootMode();
  UpdateState GetState();

}  // namespace OpenShock::OtaUpdateManager
