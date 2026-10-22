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
// The compiled-in copy (BoardDescriptorImage.cpp) is a placeholder with no board name and
// no pins. Pins only ever come from a stamp, so an image that was never stamped, or whose
// descriptor fails validation, boots with no pins rather than with pins it assumed.
//
// The layout is shared with scripts/utils/espimage.py (DESC_FORMAT); change both together
// and bump OPENSHOCK_BOARD_DESCRIPTOR_SCHEMA_VERSION. Never read the global directly:
// the compiler would fold the placeholder values into the code and the stamped bytes
// would never be seen. Go through OpenShock::Board::Get().

#include "Checksum.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>

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
    /// @brief The number of leading bytes the crc32 field covers: everything before itself.
    constexpr std::size_t kCrcCoverage = offsetof(BoardDescriptor, crc32);

    /// @brief The descriptor with its crc32 filled in (Checksum::Crc32, what scripts/utils/espimage.py checks against).
    constexpr BoardDescriptor Seal(BoardDescriptor desc)
    {
      desc.crc32 = 0;
      auto bytes = std::bit_cast<std::array<uint8_t, OPENSHOCK_BOARD_DESCRIPTOR_SIZE>>(desc);
      desc.crc32 = Checksum::Crc32(bytes.data(), kCrcCoverage);
      return desc;
    }

    /// @brief A sealed descriptor that describes no board: `name` (empty for the placeholder compiled into the
    /// image, "UNSTAMPED" for the runtime fallback), the given chip, no pins, no flags, no bypasses.
    ///
    /// These are the safe defaults: with no pins the hub drives nothing it was never told about, and still boots
    /// into the captive portal.
    constexpr BoardDescriptor Defaults(const char* name, const char* chip)
    {
      BoardDescriptor desc {};
      desc.magic         = OPENSHOCK_BOARD_DESCRIPTOR_MAGIC;
      desc.schemaVersion = OPENSHOCK_BOARD_DESCRIPTOR_SCHEMA_VERSION;
      desc.length        = OPENSHOCK_BOARD_DESCRIPTOR_SIZE;
      for (std::size_t i = 0; i + 1 < sizeof(desc.board) && name[i] != '\0'; i++) desc.board[i] = name[i];
      for (std::size_t i = 0; i + 1 < sizeof(desc.chip) && chip[i] != '\0'; i++) desc.chip[i] = chip[i];
      desc.rfTxGpio       = -1;
      desc.estopGpio      = -1;
      desc.ledGpio        = -1;
      desc.ws2812bGpio    = -1;
      desc.flags          = 0;
      desc.gpioBypassMask = 0;
      return Seal(desc);
    }

    /// @brief The descriptor of the running image, validated once on first use.
    ///
    /// The image is compiled with a placeholder (Defaults() with no name) that scripts/stamp_board.py replaces
    /// per board. When the image carries no stamped descriptor - never stamped, or magic, length or CRC wrong -
    /// this is Defaults() named "UNSTAMPED": no pins, so the hub boots into the captive portal and says what is
    /// wrong instead of driving pins it was never told about.
    const BoardDescriptor& Get();

    /// @brief Whether Get() is the image's own descriptor rather than the fallback.
    bool IsStamped();

    /// @brief Get().board as a view, measured once. Points into the descriptor, so it stays valid and NUL-terminated.
    std::string_view Name();

    /// @brief Get().chip as a view, measured once. Points into the descriptor, so it stays valid and NUL-terminated.
    std::string_view Chip();

    enum class ParseResult : uint8_t {
      Absent,     ///< Not a descriptor at all (no magic): an empty slot, or a build from before descriptors existed.
      Invalid,    ///< Has the magic but fails validation (length, schema, CRC, bounds): corrupt or mis-stamped.
      Unstamped,  ///< A valid placeholder with no board name: an image that was built but never stamped.
      Valid,      ///< A stamped descriptor; `out` is usable.
    };

    /// @brief Decodes and validates descriptor bytes read from elsewhere (another OTA slot).
    /// @return What the bytes are. `out` is only meaningful for Valid (and holds the placeholder for Unstamped).
    ParseResult Parse(const uint8_t (&bytes)[OPENSHOCK_BOARD_DESCRIPTOR_SIZE], BoardDescriptor& out);

    inline bool IsBypassedGPIO(int8_t pin)
    {
      return pin >= 0 && pin < 64 && (Get().gpioBypassMask & (1ULL << pin)) != 0;
    }
  }  // namespace Board
}  // namespace OpenShock
