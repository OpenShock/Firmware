# Compile crypto's PURE sources directly. Base64 pulls in common headers
# (TinyVec.h -> OpenShock.h, Logging.h), included by path below; Logging.h and the
# generated openshock_board.h come from test/host_support.
idf_component_register(
  SRCS         "test_main.cpp"
               "test_base64.cpp"
               "../../src/Base64.cpp"
  INCLUDE_DIRS "."
               "../../include"             # crypto headers (Hashing.h, Base64.h)
               "../../../common/include"   # TinyVec.h, OpenShock.h
  REQUIRES     unity
               host_support                # Logging.h, openshock_board.h (test/host_support)
               mbedtls                     # Base64.cpp -> mbedtls/base64.h
  WHOLE_ARCHIVE  # keep the auto-registered TEST_CASEs from being stripped
)

# ESP-IDF 6.1 linux-target link order: tf-psa-crypto's platform.c (linked last)
# calls the IDF mbedtls port's esp_mbedtls_mem_calloc/free (port/esp_mem.c), but that
# archive comes earlier on the link line, so nothing has pulled esp_mem.o in yet.
# Mark the two symbols undefined up front, the way IDF itself forces its own
# mbedtls_psa_crypto_init_include_impl.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u esp_mbedtls_mem_calloc" "-u esp_mbedtls_mem_free")
