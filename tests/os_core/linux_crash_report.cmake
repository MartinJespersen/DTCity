set(probe "${PROBE}")
if(MODE STREQUAL "quoted_path")
  set(probe_dir "${WORK_DIR}/crash probe with spaces 'quote'")
  file(MAKE_DIRECTORY "${probe_dir}")
  file(COPY "${PROBE}" DESTINATION "${probe_dir}")
  get_filename_component(probe_name "${PROBE}" NAME)
  set(probe "${probe_dir}/${probe_name}")
endif()

if(MODE STREQUAL "no_symbolizer")
  set(ENV{PATH} "")
elseif(MODE STREQUAL "stalled_symbolizer")
  set(symbolizer_dir "${WORK_DIR}/stalled symbolizer")
  file(MAKE_DIRECTORY "${symbolizer_dir}")
  file(CREATE_LINK "${PROBE}" "${symbolizer_dir}/llvm-symbolizer" SYMBOLIC)
  set(ENV{PATH} "${symbolizer_dir}")
endif()
execute_process(COMMAND "${probe}" "${MODE}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error TIMEOUT 10)
set(report "${output}${error}")
set(expected_signal SIGILL)
set(expected_result "Illegal instruction")
if(MODE STREQUAL "abort")
  set(expected_signal SIGABRT)
  set(expected_result "Subprocess aborted")
elseif(MODE STREQUAL "segv" OR MODE STREQUAL "overflow")
  set(expected_signal SIGSEGV)
  set(expected_result "Segmentation fault")
endif()

if(NOT result STREQUAL expected_result OR
   NOT report MATCHES "Fatal Signal" OR
   NOT report MATCHES "${expected_signal}" OR
   NOT report MATCHES "Callstack:" OR
   NOT report MATCHES "1\\. \\[0x[0-9a-f]+\\]" OR
   NOT report MATCHES "Version:")
  message(FATAL_ERROR "Expected ${expected_signal} with a complete crash report for ${MODE}, got ${result}:\n${report}")
endif()
if(MODE STREQUAL "no_symbolizer")
  if(NOT report MATCHES "llvm-symbolizer unavailable")
    message(FATAL_ERROR "Expected raw-address fallback:\n${report}")
  endif()
elseif(SYMBOLIZER_AVAILABLE AND NOT MODE STREQUAL "abort" AND NOT MODE STREQUAL "segv" AND
       NOT MODE STREQUAL "stalled_symbolizer")
  set(expected_function "_os_lnx_crash_probe_fail")
  if(MODE STREQUAL "overflow")
    set(expected_function "_os_lnx_crash_probe_overflow")
  endif()
  if(NOT report MATCHES "${expected_function}" OR
     NOT report MATCHES "linux_crash_probe.cpp:[0-9]+")
    message(FATAL_ERROR "Expected function names and source lines:\n${report}")
  endif()
endif()
