cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Only build 'main'; the component is pure C++ over byte spans, compiled directly (see main/CMakeLists.txt).
set(COMPONENTS main)

# linux-target freertos mock, so unity's IDF integration has its dependencies.
list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/tools/mocks/freertos/")

project(partition_layout_host_test)
