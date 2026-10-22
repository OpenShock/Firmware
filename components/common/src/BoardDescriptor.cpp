#include "BoardDescriptor.h"

const char* const TAG = "Board";

#include "Logging.h"
#include "OpenShock.h"

#include <array>
#include <bit>
#include <cstring>

using namespace OpenShock;

namespace {
  constexpr uint32_t crc32(const uint8_t* data, std::size_t length)
  {
    // IEEE CRC-32, reflected, as zlib.crc32() computes it (what scripts/utils/espimage.py checks against).
    uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < length; i++) {
      crc ^= data[i];
      for (int bit = 0; bit < 8; bit++) {
        crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
      }
    }
    return ~crc;
  }

  using DescriptorBytes = std::array<uint8_t, OPENSHOCK_BOARD_DESCRIPTOR_SIZE>;

  constexpr std::size_t kCrcCoverage = OPENSHOCK_BOARD_DESCRIPTOR_SIZE - sizeof(uint32_t);

  constexpr void copyString(char* dst, std::size_t dstLen, const char* src)
  {
    std::size_t i = 0;
    for (; i + 1 < dstLen && src[i] != '\0'; i++) dst[i] = src[i];
    for (; i < dstLen; i++) dst[i] = '\0';
  }

  constexpr BoardDescriptor withCrc(BoardDescriptor desc)
  {
    desc.crc32 = 0;
    auto bytes = std::bit_cast<DescriptorBytes>(desc);
    desc.crc32 = crc32(bytes.data(), kCrcCoverage);
    return desc;
  }

  // The descriptor compiled into the image, built from the board the build was configured
  // for (openshock_board.h). A per-chip build is configured for a placeholder board and
  // scripts/stamp_board.py replaces these bytes per board; a per-board build already has
  // the right values and the stamp is a no-op.
  constexpr BoardDescriptor compiledDescriptor()
  {
    BoardDescriptor desc {};
    desc.magic         = OPENSHOCK_BOARD_DESCRIPTOR_MAGIC;
    desc.schemaVersion = OPENSHOCK_BOARD_DESCRIPTOR_SCHEMA_VERSION;
    desc.length        = OPENSHOCK_BOARD_DESCRIPTOR_SIZE;
    copyString(desc.board, sizeof(desc.board), OPENSHOCK_FW_BOARD);
    copyString(desc.chip, sizeof(desc.chip), OPENSHOCK_FW_CHIP);
    desc.rfTxGpio       = OPENSHOCK_RF_TX_GPIO;
    desc.estopGpio      = OPENSHOCK_ESTOP_PIN;
    desc.ledGpio        = OPENSHOCK_LED_GPIO;
    desc.ws2812bGpio    = OPENSHOCK_LED_WS2812B;
    desc.flags          = OPENSHOCK_LED_SWAP_RG_CHANNELS ? OPENSHOCK_BOARD_FLAG_LED_SWAP_RG : 0u;
    desc.gpioBypassMask = OPENSHOCK_GPIO_BYPASS_MASK;
    return withCrc(desc);
  }

  constexpr BoardDescriptor fallbackDescriptor()
  {
    BoardDescriptor desc {};
    desc.magic         = OPENSHOCK_BOARD_DESCRIPTOR_MAGIC;
    desc.schemaVersion = OPENSHOCK_BOARD_DESCRIPTOR_SCHEMA_VERSION;
    desc.length        = OPENSHOCK_BOARD_DESCRIPTOR_SIZE;
    copyString(desc.board, sizeof(desc.board), "UNSTAMPED");
    copyString(desc.chip, sizeof(desc.chip), OPENSHOCK_FW_CHIP);
    desc.rfTxGpio    = OPENSHOCK_GPIO_INVALID;
    desc.estopGpio   = OPENSHOCK_GPIO_INVALID;
    desc.ledGpio     = OPENSHOCK_GPIO_INVALID;
    desc.ws2812bGpio = OPENSHOCK_GPIO_INVALID;
    return withCrc(desc);
  }

  // `used` so the compiler keeps an object nothing references by name; the linker script
  // KEEPs the section and pins it to image offset 0x120 (see BoardDescriptor.h).
  [[gnu::section(".rodata_custom_desc"), gnu::used]] constexpr BoardDescriptor g_openshock_board_desc = compiledDescriptor();

  struct Loaded {
    BoardDescriptor desc;
    bool stamped;
  };

  const Loaded& load()
  {
    static const Loaded loaded = [] {
      // Read through a volatile pointer: the bytes in flash are what the stamper wrote,
      // not what the initializer above says, and at -O2 GCC would otherwise fold the
      // compile-time values straight into the code (ESP-IDF does the same for the ELF
      // SHA-256 that esptool patches into esp_app_desc_t).
      const volatile uint8_t* src = reinterpret_cast<const volatile uint8_t*>(&g_openshock_board_desc);
      uint8_t bytes[OPENSHOCK_BOARD_DESCRIPTOR_SIZE];
      for (std::size_t i = 0; i < sizeof(bytes); i++) bytes[i] = src[i];

      Loaded result {};
      result.stamped = Board::Parse(bytes, result.desc);
      if (!result.stamped) {
        OS_LOGE(TAG, "The firmware image carries no valid board descriptor; running as UNSTAMPED with no pins. Stamp it with scripts/stamp_board.py.");
        result.desc = fallbackDescriptor();
      }
      return result;
    }();
    return loaded;
  }
}  // namespace

const BoardDescriptor& Board::Get()
{
  return load().desc;
}

bool Board::IsStamped()
{
  return load().stamped;
}

bool Board::Parse(const uint8_t (&bytes)[OPENSHOCK_BOARD_DESCRIPTOR_SIZE], BoardDescriptor& out)
{
  std::memcpy(&out, bytes, sizeof(out));

  if (out.magic != OPENSHOCK_BOARD_DESCRIPTOR_MAGIC) return false;
  if (out.length != OPENSHOCK_BOARD_DESCRIPTOR_SIZE) return false;
  if (out.schemaVersion != OPENSHOCK_BOARD_DESCRIPTOR_SCHEMA_VERSION) return false;
  if (out.crc32 != crc32(bytes, kCrcCoverage)) return false;

  // Whatever the stamper wrote, the strings are bounded here.
  out.board[sizeof(out.board) - 1] = '\0';
  out.chip[sizeof(out.chip) - 1]   = '\0';
  if (out.board[0] == '\0' || out.chip[0] == '\0') return false;

  return true;
}
