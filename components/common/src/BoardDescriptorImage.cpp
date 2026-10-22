// The board descriptor placeholder compiled into the firmware image.
//
// It describes no board: no name, no pins. scripts/stamp_board.py overwrites it per board
// after linking (the root CMakeLists.txt does that for the board a local build is
// configured for), which is the only way an image ever gets pins. Host test suites do
// not compile this file; they supply a descriptor of their own (test/host_support).
//
// An external C symbol so the linker keeps it with `-u openshock_board_desc`
// (components/common/CMakeLists.txt), as the ESP-IDF app image format documentation
// prescribes for a custom structure: components are static archives, so an object
// nothing references would otherwise never be pulled into the link. `used` keeps the
// compiler from discarding it inside the object, and the linker script KEEPs the section
// and pins it to image offset 0x120. constinit makes it a hard error if this ever stops
// being constant-initialized data.
#include "BoardDescriptor.h"
#include "OpenShock.h"

extern "C" const OpenShock::BoardDescriptor openshock_board_desc;
extern "C" [[gnu::section(".rodata_custom_desc"), gnu::used]] constinit const OpenShock::BoardDescriptor openshock_board_desc = OpenShock::Board::Defaults("", OPENSHOCK_FW_CHIP);
