cmake_minimum_required(VERSION 3.16.0)

# --- OpenShock generated build headers ---------------------------------------
# gen_env_header.py writes openshock_board.h (chip, build mode, log level) and openshock_version.h
# (version, commit) into ${CMAKE_BINARY_DIR}/config. Neither is force-included: a header carrying the
# commit in every translation unit would defeat ccache on every commit.
#
# OPENSHOCK_BOARD (-D, from scripts/build.py) names the board to stamp into the image after linking.
# Empty for a variant build, whose image stays unstamped until scripts/package_boards.py stamps each board.
set(OPENSHOCK_BOARD "" CACHE STRING "OpenShock board to stamp (boards/<name>.defaults); empty for a variant build")

# idf.py passes the interpreter of the IDF Python environment as PYTHON; fall back to the one on PATH.
if(DEFINED PYTHON)
  set(_os_python ${PYTHON})
else()
  set(_os_python python)
endif()

if(DEFINED IDF_TARGET)
  set(_os_target ${IDF_TARGET})
else()
  set(_os_target $ENV{IDF_TARGET})
endif()

set(OPENSHOCK_CONFIG_DIR ${CMAKE_BINARY_DIR}/config)
set(OPENSHOCK_VERSION_HEADER ${OPENSHOCK_CONFIG_DIR}/openshock_version.h)
set(_os_gen_command
    ${_os_python} ${CMAKE_CURRENT_LIST_DIR}/scripts/gen_env_header.py
    --out-dir ${OPENSHOCK_CONFIG_DIR}
    --target ${_os_target})

execute_process(COMMAND ${_os_gen_command} RESULT_VARIABLE _os_env_rc)
if(NOT _os_env_rc EQUAL 0)
  message(FATAL_ERROR "gen_env_header.py failed (rc=${_os_env_rc})")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(firmware)

# The version comes from git and the environment, which CMake cannot track, so refresh the header on
# every build. It is written only when it changes, so an unchanged version rebuilds nothing.
add_custom_target(openshock_version_header ALL
  BYPRODUCTS ${OPENSHOCK_VERSION_HEADER}
  COMMAND ${_os_gen_command} --version-only
  COMMENT "Refreshing openshock_version.h"
  VERBATIM)
# Order the refresh ahead of Version.cpp, the only translation unit that includes it.
idf_component_get_property(_os_common_lib common COMPONENT_LIB)
add_dependencies(${_os_common_lib} openshock_version_header)

# --- Board descriptor stamp -------------------------------------------------
# Stamp the board into the .bin as soon as gen_project_binary writes it, so `idf.py flash` carries the
# board. It runs on every build, which is fine because stamping is idempotent. With secure boot this
# would have to move before signing, and scripts/utils/espimage.py learn the signature block.
if(OPENSHOCK_BOARD)
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
# partition.
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
    COMMENT "Building static filesystem image (staticfs.bin) for static0"
    VERBATIM USES_TERMINAL)

  # Register the image with the flasher so `idf.py flash` writes it to the
  # static0 offset alongside the app and partition table.
  esptool_py_flash_target_image(flash "static0" "${_staticfs_offset}" "${_staticfs_image}")
endif()
