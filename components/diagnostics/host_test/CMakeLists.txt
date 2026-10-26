cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Only build 'main'; the pure diagnostics sources are compiled straight into it.
set(COMPONENTS main)

list(APPEND EXTRA_COMPONENT_DIRS
     "$ENV{IDF_PATH}/tools/mocks/freertos/"
     "${CMAKE_CURRENT_LIST_DIR}/../../../test/host_support")

project(diagnostics_host_test)
