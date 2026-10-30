#pragma once

#include "util/FunctionRef.h"

#include <sdkconfig.h>

#include <cstddef>
#include <cstdint>

// Crash dumps and logs. Compiled in only with uploading (CONFIG_OPENSHOCK_DIAG_UPLOAD, set by a non-empty
// CONFIG_OPENSHOCK_DIAG_UPLOAD_DOMAIN) or the SD card (CONFIG_OPENSHOCK_DIAG_SDCARD); callers check CONFIG_OPENSHOCK_DIAG.
//
// Uploading: log lines at the log upload level or more severe, and errors always, are kept in a ring in RAM that
// survives a software reset, so the lines that led up to a crash are still there on the next boot. The crash itself is
// the coredump IDF's panic handler writes to the coredump partition. Both go to CONFIG_OPENSHOCK_DIAG_UPLOAD_DOMAIN only
// when their upload is turned on in the config (Config::DiagnosticsConfig); nothing is sent by default. The hub token goes with them only
// when that domain is the backend the hub is linked to.
//
// The SD card (diagnostics/SdCard.h) gets every log record and every coredump.
namespace OpenShock::Diagnostics {
  /// @brief Installs the log capture, looks for a stored coredump and, with the SD card, mounts the card and copies the
  ///        coredump to it. Call from app_main after NVS and the board descriptor, before WiFi starts.
  bool Init();

  /// @brief Starts an upload when one is due (a no-op without uploading). Cheap; call from the main loop.
  ///        `isBackendOnline` is only called when there is something to upload.
  void Update(Util::FunctionRef<bool()> isBackendOnline);

#if CONFIG_OPENSHOCK_DIAG_UPLOAD
  /// @brief Uploads whatever is pending at the next Update(), skipping the wait after a failure. What is uploaded at all
  ///        is the owner's choice, in the config (Config::DiagnosticsConfig).
  void RequestUpload();
#endif

  struct Status {
    bool coredumpStored      = false;        // A coredump is waiting in the coredump partition
    std::size_t coredumpSize = 0;
    bool coredumpOnCard      = false;        // ... and the SD card already has it
#if CONFIG_OPENSHOCK_DIAG_UPLOAD
    bool coredumpUploaded        = false;    // ... and the backend already has it
    uint64_t pendingLogBytes     = 0;        // Captured log bytes not yet uploaded
    bool uploadRunning           = false;
    const char* lastUploadResult = nullptr;  // nullptr before the first attempt
#endif
  };
  Status GetStatus();

  /// @brief Erases the stored coredump right away, whether or not anything has it.
  bool EraseCoredump();
}  // namespace OpenShock::Diagnostics
