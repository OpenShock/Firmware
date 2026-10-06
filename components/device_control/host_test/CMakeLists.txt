cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Only build 'main'; device_control's sources are compiled straight into it (no
# firmware component needs to be linked).
set(COMPONENTS main)

# ESP-IDF's CMock mocks for everything device_control calls outside the component:
# FreeRTOS queues/tasks/semaphores, the RMT driver (and <driver/gpio.h> for the real
# Chipset.h), esp_event and esp_timer. host_fakes.cpp installs callbacks on them that
# route to deterministic fakes, so a test can step the transmit / keep-alive task loops
# against a fake clock. Plus the shared host-test stand-ins (Logging.h,
# openshock_board.h, ESP32 SoC caps, CONFIG_OPENSHOCK_*).
list(APPEND EXTRA_COMPONENT_DIRS
     "$ENV{IDF_PATH}/tools/mocks/freertos/"
     "$ENV{IDF_PATH}/tools/mocks/driver/"
     "$ENV{IDF_PATH}/tools/mocks/esp_event/"
     "$ENV{IDF_PATH}/tools/mocks/esp_timer/"
     "${CMAKE_CURRENT_LIST_DIR}/../../../test/host_support")

project(device_control_host_test)
