#include "BoardDescriptor.h"

const char* const TAG = "Board";

#include "Logging.h"
#include "OpenShock.h"

#include <cstring>
#include <span>

using namespace OpenShock;

// The placeholder compiled into the image (BoardDescriptorImage.cpp; a host test suite
// supplies its own). Declared here only, so no other translation unit can read it
// directly and have the compiler fold the placeholder values into its code.
extern "C" const BoardDescriptor openshock_board_desc;

// The one writable copy of the board state. Constant-initialized to the no-pin fallback, so a read before Init()
// is safe rather than garbage; the header hands out only a const reference to it.
static constinit Board::Detail::State s_state {
  .desc    = Board::Defaults("UNSTAMPED", OPENSHOCK_FW_CHIP),
  .stamped = false,
  .name    = "UNSTAMPED",
  .chip    = OPENSHOCK_FW_CHIP,
};
const Board::Detail::State& Board::Detail::state = s_state;

bool Board::Init()
{
  // Read through a volatile pointer: the bytes in flash are what the stamper wrote, not
  // what the placeholder initializer says, and at -O2 GCC would otherwise fold the
  // compile-time values straight into the code (ESP-IDF does the same for the ELF
  // SHA-256 that esptool patches into esp_app_desc_t).
  const volatile uint8_t* src = reinterpret_cast<const volatile uint8_t*>(&openshock_board_desc);
  uint8_t bytes[OPENSHOCK_BOARD_DESCRIPTOR_SIZE];
  for (std::size_t i = 0; i < sizeof(bytes); i++) bytes[i] = src[i];

  BoardDescriptor desc;
  switch (Parse(bytes, desc)) {
    case ParseResult::Valid:
      s_state.desc    = desc;
      s_state.stamped = true;
      break;
    case ParseResult::Unstamped:
      OS_LOGE(TAG, "The firmware image was never stamped with a board; running as UNSTAMPED with no pins. Stamp it with scripts/stamp_board.py.");
      break;
    default:
      OS_LOGE(TAG, "The firmware image's board descriptor is missing or corrupt; running as UNSTAMPED with no pins.");
      break;
  }

  // Parse() / Defaults() guarantee the terminators.
  s_state.name = s_state.desc.board;
  s_state.chip = s_state.desc.chip;

  return s_state.stamped;
}

Board::ParseResult Board::Parse(const uint8_t (&bytes)[OPENSHOCK_BOARD_DESCRIPTOR_SIZE], BoardDescriptor& out)
{
  std::memcpy(&out, bytes, sizeof(out));

  if (out.magic != OPENSHOCK_BOARD_DESCRIPTOR_MAGIC) return ParseResult::Absent;
  if (out.length != OPENSHOCK_BOARD_DESCRIPTOR_SIZE) return ParseResult::Invalid;
  if (out.schemaVersion != OPENSHOCK_BOARD_DESCRIPTOR_SCHEMA_VERSION) return ParseResult::Invalid;
  if (out.crc32 != Checksum::Crc32(std::span<const uint8_t>(bytes).first(kCrcCoverage))) return ParseResult::Invalid;

  // Whatever the stamper wrote, the strings are bounded here. A stamped descriptor always
  // names a board and a chip; an empty board name is the placeholder of an unstamped image.
  out.board[sizeof(out.board) - 1] = '\0';
  out.chip[sizeof(out.chip) - 1]   = '\0';
  if (out.chip[0] == '\0') return ParseResult::Invalid;
  if (out.board[0] == '\0') return ParseResult::Unstamped;

  return ParseResult::Valid;
}
