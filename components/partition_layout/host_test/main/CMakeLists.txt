# Runs the parser and the in-place retype against the 1.x table every fielded hub carries (built in the test from
# its entries) and against deliberately broken copies of it.
idf_component_register(
  SRCS         "test_main.cpp"
               "test_partition_table.cpp"
               "../../src/PartitionTable.cpp"
               "../../src/Md5.cpp"
  INCLUDE_DIRS "."
               "../../include"             # partition_layout/PartitionTable.h
               "../../src"                 # Md5.h (known-answer test)
  REQUIRES     unity
  WHOLE_ARCHIVE  # keep the auto-registered TEST_CASEs from being stripped
)
