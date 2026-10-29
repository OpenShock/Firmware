// Host-test stand-in for the board header scripts/gen_env_header.py generates in
// firmware builds (chip, build mode, log level), plus the pins of the fixed test board:
// a classic ESP32 with the RF transmitter on GPIO4 and no E-Stop or LED. Firmware has no
// pin macros any more (pins come from the descriptor stamped into the image); here they
// seed src/TestBoardDescriptor.cpp, which Board::Get() serves to the code under test.
// The ESP32 GPIO capabilities the real Chipset.h checks pins against come from this
// component's CMakeLists.txt.
#pragma once

#define OPENSHOCK_ESTOP_PIN            -1
#define OPENSHOCK_FW_BOARD             "HostTest"
#define OPENSHOCK_FW_BOARD_HOSTTEST    1
#define OPENSHOCK_FW_CHIP              "ESP32"
#define OPENSHOCK_FW_CHIP_ESP32        1
#define OPENSHOCK_FW_MODE              "debug"
#define OPENSHOCK_GPIO_BYPASS_MASK     0x0ULL
#define OPENSHOCK_LED_GPIO             -1
#define OPENSHOCK_LED_SWAP_RG_CHANNELS 0
#define OPENSHOCK_LED_WS2812B          -1
#define OPENSHOCK_LOG_LEVEL            5
#define OPENSHOCK_RF_TX_GPIO           4
