cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Only build 'main'; it compiles the vendored littlefs core directly against a RAM block
# device (see main/CMakeLists.txt), so no firmware component needs to be registered.
set(COMPONENTS main)

# linux-target freertos mock, so unity's IDF integration has its dependencies.
list(APPEND EXTRA_COMPONENT_DIRS
     "$ENV{IDF_PATH}/tools/mocks/freertos/")

project(fs_host_test)
