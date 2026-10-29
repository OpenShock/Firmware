# Parses images the real packer (scripts/utils/wwwimage.py) wrote, so the two implementations of the format are
# checked against each other, and hand-corrupted copies of them.
idf_component_register(
  SRCS         "test_main.cpp"
               "test_www_image.cpp"
               "sha256.cpp"
               "../../src/WwwImage.cpp"
  INCLUDE_DIRS "."
               "stubs"                     # Hashing.h: OpenShock::SHA256 over the host SHA-256
               "../../include"             # www/WwwImage.h
               "../../../common/include"   # OpenShock.h
  REQUIRES     unity
               host_support                # Logging.h, openshock_board.h (test/host_support)
  WHOLE_ARCHIVE  # keep the auto-registered TEST_CASEs from being stripped
)

# The fixtures: a few assets packed by the Python packer at configure time, read by the tests at run time.
set(_fixture_dir ${CMAKE_BINARY_DIR}/fixtures)
file(REMOVE_RECURSE ${_fixture_dir})
file(MAKE_DIRECTORY ${_fixture_dir}/assets/_app/immutable ${_fixture_dir}/empty)
file(WRITE ${_fixture_dir}/assets/index.html "<!doctype html><title>OpenShock</title>")
file(WRITE ${_fixture_dir}/assets/logo.svg "<svg xmlns='http://www.w3.org/2000/svg'/>")
file(WRITE ${_fixture_dir}/assets/LICENSES.txt "MIT")
file(WRITE ${_fixture_dir}/assets/_app/immutable/chunk.abc123.js "export default 1;")
# Many short entries, to exercise the binary search
foreach(_i RANGE 0 39)
  file(WRITE ${_fixture_dir}/assets/many/f${_i}.txt "file ${_i}")
endforeach()

if(DEFINED PYTHON)
  set(_python ${PYTHON})
else()
  find_program(_python NAMES python3 python REQUIRED)
endif()
set(_scripts_dir ${CMAKE_CURRENT_LIST_DIR}/../../../../scripts)
execute_process(
  COMMAND ${_python} -m utils.wwwimage ${_fixture_dir}/assets ${_fixture_dir}/assets.bin --gzip
  WORKING_DIRECTORY ${_scripts_dir}
  RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "packing the www fixture failed (rc=${_rc})")
endif()
execute_process(
  COMMAND ${_python} -m utils.wwwimage ${_fixture_dir}/empty ${_fixture_dir}/empty.bin
  WORKING_DIRECTORY ${_scripts_dir}
  RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "packing the empty www fixture failed (rc=${_rc})")
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE "WWW_FIXTURE_DIR=\"${_fixture_dir}\"")
