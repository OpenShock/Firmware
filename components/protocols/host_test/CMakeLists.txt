cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# main compiles protocols' pure encoder sources directly (see main/CMakeLists.txt),
# so no firmware component needs to be registered.
set(COMPONENTS main)

# linux-target mocks: freertos (unity's IDF integration), esp_timer (Temporal.h's
# millis(), used by the T330 rolling state), and driver, whose include
# dirs carry the real <driver/rmt_types.h> / <hal/rmt_types.h> (rmt_symbol_word_t) on
# the linux target. Plus the shared host-test stand-ins (Logging.h, openshock_board.h,
# CONFIG_OPENSHOCK_*).
list(APPEND EXTRA_COMPONENT_DIRS
     "$ENV{IDF_PATH}/tools/mocks/freertos/"
     "$ENV{IDF_PATH}/tools/mocks/driver/"
     "$ENV{IDF_PATH}/tools/mocks/esp_timer/"
     "${CMAKE_CURRENT_LIST_DIR}/../../../test/host_support")

project(protocols_host_test)
