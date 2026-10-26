#pragma once

#include "util/FunctionRef.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Crash dumps and logs, kept on the hub and - only when the owner opts in - uploaded to the backend.
//
// Log lines (OS_LOG* / OS_PANIC*, and ESP-IDF's own) at the log upload level or more severe, and errors always, are
// kept in a ring in RAM that survives a software reset, so the lines that led up to a crash are still there on the next
// boot. The crash itself is the coredump IDF's panic handler writes to the coredump partition. Both stay on the hub
// unless their upload is turned on (`diag` serial command); nothing is sent by default.
//
// Uploads go to the backend the hub is linked to, or to the override domain when one is set. The hub token goes only to
// the linked backend: an upload to another domain carries none.
//
// Only official firmware (built by OpenShock's CI, Constants::FW_OFFICIAL) uploads by default, so a local or fork build
// doesn't send crashes of code nobody else has to the linked backend. Such a build uploads only to an override domain,
// which takes a deliberate `diag domain`.
//
// Builds with CONFIG_OPENSHOCK_DIAG_SDCARD also write every log record and every coredump to an SD card
// (diagnostics/SdCard.h).
namespace OpenShock::Diagnostics {
  /// @brief Installs the log capture, looks for a stored coredump and, with SD support, mounts the card and copies the
  ///        coredump to it. Call from app_main after NVS and the board descriptor, before WiFi starts.
  bool Init();

  /// @brief Starts an upload when one is due. Cheap; call from the main loop. `isBackendOnline` is only called when
  ///        there is something to upload.
  void Update(Util::FunctionRef<bool()> isBackendOnline);

  bool IsCoredumpUploadEnabled();
  bool SetCoredumpUploadEnabled(bool enabled);

  /// @brief The least severe log level that is uploaded; None uploads no logs.
  enum class LogLevel : uint8_t {
    None    = 0,
    Error   = 1,
    Warn    = 2,
    Info    = 3,
    Debug   = 4,
    Verbose = 5,
  };
  const char* LogLevelName(LogLevel level);
  bool TryParseLogLevel(std::string_view name, LogLevel& out);

  LogLevel GetLogUploadLevel();
  bool SetLogUploadLevel(LogLevel level);

  /// @brief Domain uploads go to instead of the linked backend (host, optionally with :port); empty for the backend.
  std::string GetUploadDomain();
  /// @brief Sets or (with an empty domain) clears the override. False for a malformed domain or a failed save.
  bool SetUploadDomain(std::string_view domain);

  /// @brief Whether this firmware may upload at all: official firmware, or any firmware with an override domain set.
  bool UploadsAllowed();

  /// @brief Uploads whatever is pending at the next Update(), skipping the wait after a failure.
  void RequestUpload();

  struct Status {
    bool coredumpStored;           // A coredump is waiting in the coredump partition
    std::size_t coredumpSize;
    bool coredumpUploaded;         // ... and the backend already has it
    bool coredumpOnCard;           // ... and the SD card already has it
    uint64_t pendingLogBytes;      // Captured log bytes not yet uploaded
    bool uploadsAllowed;           // UploadsAllowed()
    bool uploadRunning;
    const char* lastUploadResult;  // nullptr before the first attempt
  };
  Status GetStatus();

  /// @brief Erases the stored coredump right away, whether or not anything has it.
  bool EraseCoredump();
}  // namespace OpenShock::Diagnostics
