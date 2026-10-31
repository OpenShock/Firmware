cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Only build 'main'; the component is pure C++ over byte spans plus Hashing.h, compiled directly (see main/CMakeLists.txt).
set(COMPONENTS main)

# linux-target freertos mock, so unity's IDF integration has its dependencies, and
# the shared host-test stand-ins (openshock_board.h, CONFIG_OPENSHOCK_*).
list(APPEND EXTRA_COMPONENT_DIRS
     "$ENV{IDF_PATH}/tools/mocks/freertos/"
     "${CMAKE_CURRENT_LIST_DIR}/../../../test/host_support")

project(partition_layout_host_test)
include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/cxx_standard.cmake)
