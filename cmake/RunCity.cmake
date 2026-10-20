# Match the shared city preset's host-specific output directory.
if(NOT DEFINED city_preset)
  set(city_preset "debug-no-asan")
endif()
set(city_dir "${CMAKE_CURRENT_LIST_DIR}/../build/${CMAKE_HOST_SYSTEM_NAME}/${city_preset}")
set(city_executable "${city_dir}/city")
if(CMAKE_HOST_WIN32)
  string(APPEND city_executable ".exe")
endif()

execute_process(
  COMMAND "${city_executable}"
  WORKING_DIRECTORY "${city_dir}"
  RESULT_VARIABLE city_result
)
if(NOT city_result STREQUAL "0")
  message(FATAL_ERROR "City failed: ${city_result}")
endif()
