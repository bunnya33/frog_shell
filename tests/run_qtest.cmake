# Qt's Windows entry point may not expose stdout to CTest. Always retain a report.
file(WRITE "${TEST_REPORT}" "")
if(NOT DEFINED TEST_TIMEOUT)
  set(TEST_TIMEOUT 90)
endif()
execute_process(COMMAND "${TEST_EXECUTABLE}" -o "${TEST_REPORT},txt"
    RESULT_VARIABLE test_result TIMEOUT ${TEST_TIMEOUT})
if(EXISTS "${TEST_REPORT}")
  file(READ "${TEST_REPORT}" report)
  message("${report}")
endif()
if(NOT test_result STREQUAL "0" OR NOT report MATCHES "Totals:.* 0 failed,")
  message(FATAL_ERROR "Qt test failed: ${test_result}; report: ${TEST_REPORT}")
endif()
