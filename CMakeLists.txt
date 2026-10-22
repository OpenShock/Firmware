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
# Cached so an automatic reconfigure (e.g. from a plain `ninja` run without the env var) keeps generating the
# headers for the same board instead of silently dropping them.
if(NOT DEFINED OPENSHOCK_BOARD AND DEFINED ENV{OPENSHOCK_BOARD})
  set(OPENSHOCK_BOARD $ENV{OPENSHOCK_BOARD} CACHE STRING "OpenShock board (boards/<name>.defaults)")
endif()

# idf.py passes the interpreter of the IDF Python environment as PYTHON; fall back to the one on PATH.
if(DEFINED PYTHON)
  set(_os_python ${PYTHON})
else()
  set(_os_python python)
endif()

if(DEFINED OPENSHOCK_BOARD)
  # The chip label (OPENSHOCK_FW_CHIP) is derived from the fragment's CONFIG_IDF_TARGET by
  # gen_env_header.py through the one target-to-chip map in scripts/utils/boards.py, the same
  # map the stamped descriptor's chip field uses. The build's IDF_TARGET is passed along only
  # so a fragment configured for another target fails here.
  if(DEFINED IDF_TARGET)
    set(_os_target ${IDF_TARGET})
  elseif(DEFINED ENV{IDF_TARGET})
    set(_os_target $ENV{IDF_TARGET})
  else()
    set(_os_target "esp32")
  endif()

  set(OPENSHOCK_CONFIG_DIR ${CMAKE_BINARY_DIR}/config)
  set(OPENSHOCK_VERSION_HEADER ${OPENSHOCK_CONFIG_DIR}/openshock_version.h)
  set(_os_gen_command
      ${_os_python} ${CMAKE_CURRENT_LIST_DIR}/scripts/gen_env_header.py
      --out-dir ${OPENSHOCK_CONFIG_DIR}
      --board ${OPENSHOCK_BOARD}
      --target ${_os_target}
      --fragment ${CMAKE_CURRENT_LIST_DIR}/boards/${OPENSHOCK_BOARD}.defaults)

  execute_process(COMMAND ${_os_gen_command} --emit both RESULT_VARIABLE _os_env_rc)
  if(NOT _os_env_rc EQUAL 0)
    message(FATAL_ERROR "gen_env_header.py failed (rc=${_os_env_rc})")
  endif()

  # openshock_board.h (chip label, build mode, log level) is generated at configure time from the board
  # fragment, so editing the fragment must trigger a reconfigure.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/boards/${OPENSHOCK_BOARD}.defaults)
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
  foreach(_os_consumer common)  # Version.cpp is the only translation unit including it
    idf_component_get_property(_os_lib ${_os_consumer} COMPONENT_LIB)
    if(_os_lib)
      add_dependencies(${_os_lib} openshock_version_header)
    endif()
  endforeach()

  # --- Board descriptor stamp -------------------------------------------------
  # The compiled image describes no board: its descriptor (BoardDescriptor.h, at image
  # offset 0x120) is a placeholder with no name and no pins. Stamp the configured board
  # into firmware.bin right after esptool produces it, so `idf.py flash` and the CI
  # artifacts carry the board, and a build that somehow skipped this boots with no pins
  # rather than with pins it assumed. gen_project_binary is the IDF target that writes
  # the .bin. A POST_BUILD step on a custom target runs on every build, not only when the
  # ELF changed; that is fine because the stamp is idempotent (CI checks that re-stamping
  # is byte-identical). If app signing (secure boot) is ever enabled, this has to move
  # before the signing step, and scripts/utils/espimage.py has to learn the signature
  # block: today it rejects an image with anything after the SHA-256.
  idf_build_get_property(_os_build_dir BUILD_DIR)
  add_custom_command(TARGET gen_project_binary POST_BUILD
    COMMAND ${_os_python} ${CMAKE_CURRENT_LIST_DIR}/scripts/stamp_board.py stamp
            --image ${_os_build_dir}/${CMAKE_PROJECT_NAME}.bin
            --board ${OPENSHOCK_BOARD}
    COMMENT "Stamping board descriptor for ${OPENSHOCK_BOARD} into ${CMAKE_PROJECT_NAME}.bin"
    VERBATIM)
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
  message(WARNING "No `static0` partition in the active partition table; "
                  "skipping static filesystem image generation.")
else()
  set(_staticfs_image ${CMAKE_BINARY_DIR}/staticfs.bin)

  # ALL target so a normal `idf.py build` regenerates the image. The generator only
  # runs pnpm when frontend/build is older than the frontend sources (never in CI,
  # where the frontend is downloaded as an artifact), so this stays cheap on
  # incremental builds. Staging lives in the build directory so concurrent board
  # builds don't share (and wipe) one directory.
  add_custom_target(staticfs ALL
    BYPRODUCTS ${_staticfs_image}
    COMMAND ${_os_python} ${CMAKE_CURRENT_LIST_DIR}/scripts/gen_staticfs.py
            --frontend-dir   ${CMAKE_CURRENT_LIST_DIR}/frontend
            --staging-dir    ${CMAKE_BINARY_DIR}/staticfs
            --output         ${_staticfs_image}
            --partition-size ${_staticfs_size}
            --block-size 4096 --read-size 128 --prog-size 128
    COMMENT "Building static filesystem image (staticfs.bin) for static0"
    VERBATIM USES_TERMINAL)

  # Register the image with the flasher so `idf.py flash` writes it to the
  # static0 offset alongside the app and partition table.
  esptool_py_flash_target_image(flash "static0" "${_staticfs_offset}" "${_staticfs_image}")
endif()
