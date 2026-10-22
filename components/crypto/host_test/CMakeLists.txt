cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Only build 'main'; the crypto/common headers are pulled in by include path.
set(COMPONENTS main)

# linux-target freertos mock, so unity's IDF integration has its dependencies, and
# the shared host-test stand-ins (Logging.h, openshock_board.h, CONFIG_OPENSHOCK_*).
list(APPEND EXTRA_COMPONENT_DIRS
     "$ENV{IDF_PATH}/tools/mocks/freertos/"
     "${CMAKE_CURRENT_LIST_DIR}/../../../test/host_support")

project(crypto_host_test)
