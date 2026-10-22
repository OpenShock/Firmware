#pragma once

// The board descriptor: what makes this firmware image belong to one board.
//
// The firmware is compiled once per chip. Everything that differs between boards on the
// same chip - the pins, the LED flags, the board name OTA asks the repository server
// for - lives in this 256-byte struct, placed in the `.rodata_custom_desc` section.
// ESP-IDF's linker script puts that section at a fixed offset of the app image (0x120,
// right after esp_app_desc_t), so scripts/stamp_board.py can overwrite it per board
// after linking, fix up the image checksum and SHA-256, and turn one compile into an
// image for every board on that chip.
//
// Because it is part of the app image it rides along with every OTA update and lives
// in whichever OTA slot the app does; formatting the config, NVS or static partitions
// cannot touch it, and a rollback restores the old app together with its descriptor.
//
// The layout is shared with scripts/utils/espimage.py (DESC_FORMAT); change both together
// and bump OPENSHOCK_BOARD_DESCRIPTOR_SCHEMA_VERSION. Never read the global directly:
// the compiler would fold the compiled-in placeholder values into the code and the
// stamped bytes would never be seen. Go through OpenShock::Board::Get().

#include <cstddef>
#include <cstdint>

#define OPENSHOCK_BOARD_DESCRIPTOR_MAGIC          0x4442534Fu  // "OSBD" as little-endian bytes
#define OPENSHOCK_BOARD_DESCRIPTOR_SCHEMA_VERSION 1u
#define OPENSHOCK_BOARD_DESCRIPTOR_SIZE           256u
#define OPENSHOCK_BOARD_DESCRIPTOR_IMAGE_OFFSET   0x120u  // sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)

// BoardDescriptor::flags bits.
#define OPENSHOCK_BOARD_FLAG_LED_SWAP_RG (1u << 0)  // The WS2812B expects RGB rather than GRB order

namespace OpenShock {
  struct alignas(8) BoardDescriptor {
    uint32_t magic;          // OPENSHOCK_BOARD_DESCRIPTOR_MAGIC
    uint16_t schemaVersion;  // OPENSHOCK_BOARD_DESCRIPTOR_SCHEMA_VERSION
    uint16_t length;         // OPENSHOCK_BOARD_DESCRIPTOR_SIZE
    char board[48];          // NUL-terminated board name, e.g. "OpenShock-Core-V2"; the OTA identity
    char chip[16];           // NUL-terminated chip label, e.g. "ESP32-S3"; must equal OPENSHOCK_FW_CHIP
    int8_t rfTxGpio;         // -1 when the board has no such pin
    int8_t estopGpio;
    int8_t ledGpio;
    int8_t ws2812bGpio;
    uint32_t flags;           // OPENSHOCK_BOARD_FLAG_*
    uint64_t gpioBypassMask;  // Chip-unsafe GPIOs this board wires up anyway (bit n = GPIO n); see Chipset.h
    uint8_t reserved[164];    // Zero; room for later schema versions without moving crc32
    uint32_t crc32;           // IEEE CRC-32 (zlib) of the 252 bytes before it
  };

  static_assert(sizeof(BoardDescriptor) == OPENSHOCK_BOARD_DESCRIPTOR_SIZE);
  static_assert(offsetof(BoardDescriptor, board) == 8);
  static_assert(offsetof(BoardDescriptor, chip) == 56);
  static_assert(offsetof(BoardDescriptor, rfTxGpio) == 72);
  static_assert(offsetof(BoardDescriptor, flags) == 76);
  static_assert(offsetof(BoardDescriptor, gpioBypassMask) == 80);
  static_assert(offsetof(BoardDescriptor, reserved) == 88);
  static_assert(offsetof(BoardDescriptor, crc32) == 252);

  namespace Board {
    /// @brief The descriptor of the running image, validated once on first use.
    ///
    /// When the image carries no valid descriptor (magic, length or CRC wrong) this is a
    /// fallback named "UNSTAMPED" with no pins, so the hub still boots into the captive
    /// portal and says what is wrong instead of driving pins it was never told about.
    const BoardDescriptor& Get();

    /// @brief Whether Get() is the image's own descriptor rather than the fallback.
    bool IsStamped();

    /// @brief Decodes and validates descriptor bytes read from elsewhere (another OTA slot).
    /// @return false when they are not a descriptor this firmware understands. `out` is then unspecified.
    bool Parse(const uint8_t (&bytes)[OPENSHOCK_BOARD_DESCRIPTOR_SIZE], BoardDescriptor& out);

    inline bool IsBypassedGPIO(int8_t pin)
    {
      return pin >= 0 && pin < 64 && (Get().gpioBypassMask & (1ULL << pin)) != 0;
    }
  }  // namespace Board
}  // namespace OpenShock
