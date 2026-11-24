cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(vinyl_lyrics)

idf_build_get_property(vinyl_python PYTHON)

# Keep the old, RAM-tuned Bluetooth stack while adding the missing status path.
idf_component_get_property(vinyl_bt_dir bt COMPONENT_DIR)
idf_component_get_property(vinyl_bt_target bt COMPONENT_LIB)
set(vinyl_avrc_source "${CMAKE_BINARY_DIR}/vinyl_btc_avrc.c")
execute_process(COMMAND "${vinyl_python}" "${CMAKE_SOURCE_DIR}/tools/patch_avrc.py"
  "${vinyl_bt_dir}/host/bluedroid/btc/profile/std/avrc/btc_avrc.c" "${vinyl_avrc_source}"
  RESULT_VARIABLE vinyl_avrc_patch_status)
if(NOT vinyl_avrc_patch_status EQUAL 0)
  message(FATAL_ERROR "AVRCP patch failed; review pinned SDK version")
endif()
get_target_property(vinyl_bt_sources ${vinyl_bt_target} SOURCES)
list(FILTER vinyl_bt_sources EXCLUDE REGEX "(^|/)btc_avrc\\.c$")
list(APPEND vinyl_bt_sources "${vinyl_avrc_source}")
set_property(TARGET ${vinyl_bt_target} PROPERTY SOURCES "${vinyl_bt_sources}")
target_include_directories(${vinyl_bt_target} PRIVATE "${CMAKE_SOURCE_DIR}/../shared")

