FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(
  SRCS ${app_sources}
  REQUIRES common time logging concurrency util http chipset led_drivers
)
