#include "BoardDescriptor.h"

const char* const TAG = "Board";

#include "Logging.h"
#include "OpenShock.h"

#include <cstring>

using namespace OpenShock;

// The placeholder compiled into the image (BoardDescriptorImage.cpp; a host test suite
// supplies its own). Declared here only, so no other translation unit can read it
// directly and have the compiler fold the placeholder values into its code.
extern "C" const BoardDescriptor openshock_board_desc;

namespace {
  struct Loaded {
    BoardDescriptor desc;
    bool stamped;
    std::string_view name;  // Into desc.board, measured once
    std::string_view chip;  // Into desc.chip, measured once
  };

  const Loaded& load()
  {
    static const Loaded loaded = [] {
      // Read through a volatile pointer: the bytes in flash are what the stamper wrote,
      // not what the placeholder initializer says, and at -O2 GCC would otherwise fold
      // the compile-time values straight into the code (ESP-IDF does the same for the
      // ELF SHA-256 that esptool patches into esp_app_desc_t).
      const volatile uint8_t* src = reinterpret_cast<const volatile uint8_t*>(&openshock_board_desc);
      uint8_t bytes[OPENSHOCK_BOARD_DESCRIPTOR_SIZE];
      for (std::size_t i = 0; i < sizeof(bytes); i++) bytes[i] = src[i];

      Loaded result {};
      switch (Board::Parse(bytes, result.desc)) {
        case Board::ParseResult::Valid:
          result.stamped = true;
          break;
        case Board::ParseResult::Unstamped:
          OS_LOGE(TAG, "The firmware image was never stamped with a board; running as UNSTAMPED with no pins. Stamp it with scripts/stamp_board.py.");
          break;
        default:
          OS_LOGE(TAG, "The firmware image's board descriptor is missing or corrupt; running as UNSTAMPED with no pins.");
          break;
      }
      if (!result.stamped) {
        result.desc = Board::Defaults("UNSTAMPED", OPENSHOCK_FW_CHIP);
      }
      result.name = result.desc.board;  // Parse() / Defaults() guarantee the terminator
      result.chip = result.desc.chip;
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

std::string_view Board::Name()
{
  return load().name;
}

std::string_view Board::Chip()
{
  return load().chip;
}

Board::ParseResult Board::Parse(const uint8_t (&bytes)[OPENSHOCK_BOARD_DESCRIPTOR_SIZE], BoardDescriptor& out)
{
  std::memcpy(&out, bytes, sizeof(out));

  if (out.magic != OPENSHOCK_BOARD_DESCRIPTOR_MAGIC) return ParseResult::Absent;
  if (out.length != OPENSHOCK_BOARD_DESCRIPTOR_SIZE) return ParseResult::Invalid;
  if (out.schemaVersion != OPENSHOCK_BOARD_DESCRIPTOR_SCHEMA_VERSION) return ParseResult::Invalid;
  if (out.crc32 != Checksum::Crc32(bytes, kCrcCoverage)) return ParseResult::Invalid;

  // Whatever the stamper wrote, the strings are bounded here. A stamped descriptor always
  // names a board and a chip; an empty board name is the placeholder of an unstamped image.
  out.board[sizeof(out.board) - 1] = '\0';
  out.chip[sizeof(out.chip) - 1]   = '\0';
  if (out.chip[0] == '\0') return ParseResult::Invalid;
  if (out.board[0] == '\0') return ParseResult::Unstamped;

  return ParseResult::Valid;
}
