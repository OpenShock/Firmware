cmake_minimum_required(VERSION 3.16.0)

# --- OpenShock generated build headers ---------------------------------------
# Two headers are generated into ${CMAKE_BINARY_DIR}/config, the directory ESP-IDF
# already writes sdkconfig.h into and puts on the global include path:
#
#   openshock_board.h    board/chip identity, build mode, log level, GPIO assignments
#   openshock_version.h  firmware version and git commit
#
# Neither is force-included. openshock_board.h is included by OpenShock.h, so only
# OpenShock's own components see it; openshock_version.h is included by the few
# translation units that actually report a version.
#
# They used to be one header, force-included into every translation unit in the project
# through idf_build_set_property(COMPILE_OPTIONS "-include..."). Because it carried the
# git commit and a version string ending in +<short-sha>, every commit changed the
# preprocessed text of every source file in the build - all of ESP-IDF included - and
# ccache could never reuse a single object between CI runs.
if(NOT DEFINED OPENSHOCK_BOARD AND DEFINED ENV{OPENSHOCK_BOARD})
  set(OPENSHOCK_BOARD $ENV{OPENSHOCK_BOARD})
endif()

if(DEFINED OPENSHOCK_BOARD)
  # Map the IDF target to an OpenShock chip label for OPENSHOCK_FW_CHIP.
  if(DEFINED IDF_TARGET)
    set(_os_target ${IDF_TARGET})
  elseif(DEFINED ENV{IDF_TARGET})
    set(_os_target $ENV{IDF_TARGET})
  else()
    set(_os_target "esp32")
  endif()
  if(_os_target STREQUAL "esp32s3")
    set(_os_chip "ESP32-S3")
  elseif(_os_target STREQUAL "esp32s2")
    set(_os_chip "ESP32-S2")
  elseif(_os_target STREQUAL "esp32c3")
    set(_os_chip "ESP32-C3")
  else()
    set(_os_chip "ESP32")
  endif()

  set(OPENSHOCK_CONFIG_DIR ${CMAKE_BINARY_DIR}/config)
  set(OPENSHOCK_VERSION_HEADER ${OPENSHOCK_CONFIG_DIR}/openshock_version.h)
  set(_os_gen_command
      ${CMAKE_COMMAND} -E env
      python ${CMAKE_CURRENT_LIST_DIR}/scripts/gen_env_header.py
      --out-dir ${OPENSHOCK_CONFIG_DIR}
      --board ${OPENSHOCK_BOARD}
      --chip ${_os_chip}
      --fragment ${CMAKE_CURRENT_LIST_DIR}/boards/${OPENSHOCK_BOARD}.defaults)

  execute_process(COMMAND ${_os_gen_command} --emit both RESULT_VARIABLE _os_env_rc)
  if(NOT _os_env_rc EQUAL 0)
    message(FATAL_ERROR "gen_env_header.py failed (rc=${_os_env_rc})")
  endif()
  set(OPENSHOCK_NATIVE_ENV TRUE)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(firmware)

if(OPENSHOCK_NATIVE_ENV)
  # The version and commit come from git and the environment, which CMake has no
  # dependency on, so refresh the version header on every build rather than only on a
  # reconfigure. Generating it at configure time alone left an incremental build
  # reporting whatever version the build directory was first configured with.
  #
  # The generator writes only when the contents differ, so an unchanged version leaves
  # the file - and its mtime - alone, and nothing downstream is rebuilt.
  add_custom_target(openshock_version_header ALL
    BYPRODUCTS ${OPENSHOCK_VERSION_HEADER}
    COMMAND ${_os_gen_command} --emit version
    COMMENT "Refreshing openshock_version.h"
    VERBATIM)

  # Order the refresh ahead of the components that include the header. Without this the
  # ALL target is unordered against them and a build could compile the previous version.
  foreach(_os_consumer common core serial_console)
    idf_component_get_property(_os_lib ${_os_consumer} COMPONENT_LIB)
    if(_os_lib)
      add_dependencies(${_os_lib} openshock_version_header)
    endif()
  endforeach()
endif()

# --- Static filesystem image (captive-portal web assets) ---------------------
# Build the SvelteKit frontend, gzip it, and pack it into a littlefs image whose
# geometry matches the runtime mount in components/fs/src/LfsPartition.cpp (4 KiB
# blocks, 128-byte read/prog). `idf.py flash` writes it to the `static0`
# partition. Replaces PlatformIO's build_frontend.py post-action + mklittlefs.
#
# The partition only exists in the OpenShock custom partition CSV; if the active
# partition table has no `static0` (e.g. a bare default-table build), skip the
# image instead of failing the whole build.
partition_table_get_partition_info(_staticfs_offset "--partition-name static0" "offset")
partition_table_get_partition_info(_staticfs_size   "--partition-name static0" "size")

if(_staticfs_offset STREQUAL "" OR _staticfs_size STREQUAL "")
  message(WARNING "No `static0` partition in the active partition table — "
                  "skipping static filesystem image generation.")
else()
  set(_staticfs_image ${CMAKE_BINARY_DIR}/staticfs.bin)

  # ALL target so a normal `idf.py build` regenerates the image. The generator
  # skips the pnpm rebuild when frontend/build already exists (or in CI, where the
  # frontend is downloaded as an artifact), so this stays cheap on incremental builds.
  add_custom_target(staticfs ALL
    BYPRODUCTS ${_staticfs_image}
    COMMAND ${CMAKE_COMMAND} -E env
            python ${CMAKE_CURRENT_LIST_DIR}/scripts/gen_staticfs.py
            --frontend-dir   ${CMAKE_CURRENT_LIST_DIR}/frontend
            --staging-dir    ${CMAKE_CURRENT_LIST_DIR}/staticfs
            --output         ${_staticfs_image}
            --partition-size ${_staticfs_size}
            --block-size 4096 --read-size 128 --prog-size 128
    COMMENT "Building static filesystem image (staticfs.bin) for static0"
    VERBATIM USES_TERMINAL)

  # Register the image with the flasher so `idf.py flash` writes it to the
  # static0 offset alongside the app and partition table.
  esptool_py_flash_target_image(flash "static0" "${_staticfs_offset}" "${_staticfs_image}")
endif()
