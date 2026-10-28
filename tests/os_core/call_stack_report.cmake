execute_process(COMMAND "${PROBE}" RESULT_VARIABLE result ERROR_VARIABLE report TIMEOUT 15)
if(NOT result EQUAL 0 OR NOT report MATCHES "Call stack" OR NOT report MATCHES "1\\. \\[.*[0-9a-fA-F].*\\]")
  message(FATAL_ERROR "Call stack printer failed (${result}):\n${report}")
endif()
