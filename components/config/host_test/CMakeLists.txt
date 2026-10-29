cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# main compiles config's sources (and the few osjson/common sources they need)
# directly, so no firmware component needs to be registered.
set(COMPONENTS main)

# No freertos mock: this suite runs on ESP-IDF's real FreeRTOS POSIX port, so the real
# SimpleMutexes guard the Config:: store exactly as on the device, and the real nvs_flash
# and esp_partition (linux flash emulation) hold it. The esp_wifi mock provides the real
# <esp_wifi_types.h> (wifi_auth_mode_t), which has no linux implementation; it in turn
# needs the esp_netif and lwip mocks. Plus the shared host-test stand-ins (Logging.h,
# openshock_board.h, CONFIG_OPENSHOCK_*).
list(APPEND EXTRA_COMPONENT_DIRS
     "$ENV{IDF_PATH}/tools/mocks/esp_wifi/"
     "$ENV{IDF_PATH}/tools/mocks/esp_netif/"
     "$ENV{IDF_PATH}/tools/mocks/lwip/"
     "${CMAKE_CURRENT_LIST_DIR}/../../../test/host_support")

project(config_host_test)
