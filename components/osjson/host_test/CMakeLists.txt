cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Only build 'main' and its dependency closure (osjson + jsmn + json_generator).
set(COMPONENTS main)

# Make the OpenShock components (osjson) discoverable, and use the linux-target
# freertos mock so app_main is provided on the host.
list(APPEND EXTRA_COMPONENT_DIRS
     "${CMAKE_CURRENT_LIST_DIR}/../.."          # firmware/components (contains osjson)
     "$ENV{IDF_PATH}/tools/mocks/freertos/")

project(osjson_host_test)
