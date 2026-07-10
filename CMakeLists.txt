cmake_minimum_required(VERSION 3.16.0)

# --- OpenShock native ESP-IDF environment header -----------------------------
# When a board is selected via OPENSHOCK_BOARD (set by the idf.py build driver /
# CI), generate the OpenShock environment header (firmware version, API/CDN
# domains, board/chip identity, log level) and force-include it into every
# translation unit. This replaces the PlatformIO embed_env_vars.py extra_script.
#
# The legacy PlatformIO build leaves OPENSHOCK_BOARD unset and supplies the same
# macros via -D, so everything below is skipped there and the two builds don't
# fight over the definitions.
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

  set(OPENSHOCK_ENV_HEADER ${CMAKE_BINARY_DIR}/config/openshock_env.h)
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E env
            python ${CMAKE_CURRENT_LIST_DIR}/scripts/gen_env_header.py
            --output ${OPENSHOCK_ENV_HEADER}
            --board ${OPENSHOCK_BOARD}
            --chip ${_os_chip}
    RESULT_VARIABLE _os_env_rc
  )
  if(NOT _os_env_rc EQUAL 0)
    message(FATAL_ERROR "gen_env_header.py failed (rc=${_os_env_rc})")
  endif()
  set(OPENSHOCK_NATIVE_ENV TRUE)
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

if(OPENSHOCK_NATIVE_ENV)
  idf_build_set_property(COMPILE_OPTIONS "-include${OPENSHOCK_ENV_HEADER}" APPEND)
endif()

project(firmware)

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
