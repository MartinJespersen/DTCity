execute_process(COMMAND "${PROBE}" "${MODE}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 30)
set(report "${output}${error}")
if(result EQUAL 0 OR NOT report MATCHES "ERROR: AddressSanitizer: (use-after-poison|heap-buffer-overflow|heap-use-after-free|unknown-crash)")
  message(FATAL_ERROR "Expected an ASan memory-access report for ${MODE}, got ${result}:\n${report}")
endif()
