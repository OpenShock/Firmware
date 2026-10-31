# Runs the parser and the in-place retype against the 1.x table every fielded hub carries (built in the test from
# its entries) and against deliberately broken copies of it.
idf_component_register(
  SRCS         "test_main.cpp"
               "test_partition_table.cpp"
               "../../src/PartitionTable.cpp"
  INCLUDE_DIRS "."
               "../../include"             # partition_layout/PartitionTable.h
               "../../../crypto/include"   # Hashing.h
               "../../../common/include"   # OpenShock.h
  REQUIRES     unity
               host_support                # openshock_board.h (test/host_support)
               mbedtls                     # Hashing.h -> mbedtls/md.h
  WHOLE_ARCHIVE  # keep the auto-registered TEST_CASEs from being stripped
)

# See components/crypto/host_test/main/CMakeLists.txt: esp_mem.o is otherwise not pulled in by the link order.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u esp_mbedtls_mem_calloc" "-u esp_mbedtls_mem_free")
