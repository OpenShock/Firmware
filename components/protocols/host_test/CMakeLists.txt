cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# main compiles protocols' pure encoder sources directly (see main/CMakeLists.txt),
# so no firmware component needs to be registered.
set(COMPONENTS main)

list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/tools/mocks/freertos/")

project(protocols_host_test)
