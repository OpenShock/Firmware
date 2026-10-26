# Runs the parser, the layout detection and the invariant checker against every table the repository ships
# (built from partitions/*.csv and partitions/legacy/*.csv by scripts/utils/partitions.py at configure time, the
# same code that writes each board's partition-table.bin) and against deliberately broken copies of them.
idf_component_register(
  SRCS         "test_main.cpp"
               "test_partition_table.cpp"
               "../../src/PartitionTable.cpp"
               "../../src/Md5.cpp"
  INCLUDE_DIRS "."
               "../../include"             # partition_table/PartitionTable.h
               "../../src"                 # Md5.h (known-answer test)
               "../../../common/include"
  REQUIRES     unity
               host_support                # Logging.h, openshock_board.h (test/host_support)
  WHOLE_ARCHIVE  # keep the auto-registered TEST_CASEs from being stripped
)

set(_fixture_dir ${CMAKE_BINARY_DIR}/fixtures)
file(MAKE_DIRECTORY ${_fixture_dir})
if(DEFINED PYTHON)
  set(_python ${PYTHON})
else()
  find_program(_python NAMES python3 python REQUIRED)
endif()
set(_repo ${CMAKE_CURRENT_LIST_DIR}/../../../..)
foreach(_csv ota_4mb ota_8mb legacy/ota_4mb_v1)
  get_filename_component(_name ${_csv} NAME)
  execute_process(
    COMMAND ${_python} -m utils.partitions ${_repo}/partitions/${_csv}.csv ${_fixture_dir}/${_name}.bin
    WORKING_DIRECTORY ${_repo}/scripts
    RESULT_VARIABLE _rc)
  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "building the ${_csv} fixture failed (rc=${_rc})")
  endif()
endforeach()

target_compile_definitions(${COMPONENT_LIB} PRIVATE "PT_FIXTURE_DIR=\"${_fixture_dir}\"")
