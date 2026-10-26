#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// The crash dump ESP-IDF's panic handler writes to the coredump partition (ELF format, CRC-checked).
namespace OpenShock::Diagnostics::Coredump {
  struct Image {
    std::size_t address;  // Absolute flash address
    std::size_t size;
  };

  /// @brief The stored dump, if there is one and its checksum is intact. A damaged one is erased.
  std::optional<Image> Find();

  bool Read(const Image& image, uint32_t offset, uint8_t* out, std::size_t len);

  bool Erase();

  /// @brief The panic reason recorded in the dump, reduced to printable ASCII (empty if none).
  std::string PanicReason();

  /// @brief CRC-32 of the whole image: identifies a dump across reboots.
  std::optional<uint32_t> Crc(const Image& image);

  /// @brief What the dump says about the crash.
  struct Summary {
    std::string elfSha256;  // Hex, as much of the crashed app's ELF SHA-256 as the dump records (CONFIG_APP_RETRIEVE_LEN_ELF_SHA)
    uint32_t excPc;
    char excTask[16];
  };
  std::optional<Summary> GetSummary();
}  // namespace OpenShock::Diagnostics::Coredump
