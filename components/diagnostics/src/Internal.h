#pragma once

#include "diagnostics/Diagnostics.h"

#include <sdkconfig.h>

#include <cstddef>
#include <cstdint>

// Seams between the diagnostics parts: the core (Diagnostics.cpp), uploading (Upload.cpp and Uploader.cpp, only with
// CONFIG_OPENSHOCK_DIAG_UPLOAD) and the SD card storage (sdcard/SdStorage.cpp, only with CONFIG_OPENSHOCK_DIAG_SDCARD).
namespace OpenShock::Diagnostics::Detail {
  // Coredump bookkeeping (core). A stored dump is erased once every sink that should have it does: the SD card when it
  // is configured, the backend when coredump upload is enabled. With neither, it stays for reading over serial.
  enum CoredumpSink : uint8_t {
    SinkSdCard  = 1u << 0,
    SinkBackend = 1u << 1,
  };
  void MarkCoredumpDelivered(CoredumpSink sink);

  /// @brief Serializes coredump reads against the erase that follows the last delivery. Hold it around a copy.
  void LockCoredump();
  void UnlockCoredump();

  /// @brief A coredump is stored and the backend doesn't have it yet.
  bool CoredumpAwaitingBackend();

  /// @brief esp_reset_reason() of this boot, by name.
  const char* ResetReasonName(int reason);

#if CONFIG_OPENSHOCK_DIAG_UPLOAD
  void UploadInit();
  void UploadUpdate(Util::FunctionRef<bool()> isBackendOnline);
  void UploadStatus(Status& status);
  bool CoredumpUploadEnabled();
  /// @brief Log sink of the upload: keeps the line when its level is uploaded (errors always).
  void UploadCaptureLog(char level, const char* tag, std::size_t tagLen, const char* message, std::size_t len);

  bool CoredumpUploadedBefore(uint32_t crc, std::size_t size);
  void RememberCoredumpUploaded(uint32_t crc, std::size_t size);

  /// @brief Log lines kept for upload. Copies unconsumed bytes from `start` on; see RetainedRing::Peek.
  std::size_t PeekLog(uint64_t start, char* out, std::size_t outCapacity, uint64_t& from);
  uint64_t LogConsumed();
  void ConsumeLog(uint64_t upTo);

  /// @brief Runs the upload work (on the upload task).
  /// @param result Set to a short description of the outcome.
  /// @return Whether everything that was attempted succeeded.
  bool RunUploads(const char*& result);
#endif

#if CONFIG_OPENSHOCK_DIAG_SDCARD
  /// @brief Log record sink of the SD card storage. Never blocks; drops and counts when its queue is full.
  void SdCardInit();
  /// @brief Whether the card is configured, so the coredump waits for it before being erased.
  bool SdCardConfigured();
  void SdCardEnqueueLog(char level, const char* tag, std::size_t tagLen, const char* message, std::size_t len);

  /// @brief Severity letter to the card's level: 1 error .. 5 verbose (panic and N count as error and info).
  constexpr uint8_t CardLevel(char level)
  {
    switch (level) {
      case 'C':
      case 'E':
        return 1;
      case 'W':
        return 2;
      case 'I':
      case 'N':
        return 3;
      case 'D':
        return 4;
      default:
        return 5;
    }
  }
#endif
}  // namespace OpenShock::Diagnostics::Detail
