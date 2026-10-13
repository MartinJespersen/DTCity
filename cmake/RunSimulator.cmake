# Match the shared simulator presets' host-specific output directory.
if(NOT DEFINED simulator_preset)
  set(simulator_preset "debug-simulator")
endif()
set(simulator_dir "${CMAKE_CURRENT_LIST_DIR}/../simulator/build/${CMAKE_HOST_SYSTEM_NAME}/${simulator_preset}")
set(simulator_executable "${simulator_dir}/Simulator")
if(CMAKE_HOST_WIN32)
  string(APPEND simulator_executable ".exe")
endif()

execute_process(
  COMMAND "${simulator_executable}"
  WORKING_DIRECTORY "${simulator_dir}"
  RESULT_VARIABLE simulator_result
)
if(NOT simulator_result STREQUAL "0")
  message(FATAL_ERROR "Simulator failed: ${simulator_result}")
endif()
