# SPDX-License-Identifier: MIT
include_guard(GLOBAL)
function(aster_configure_distribution target)
  get_filename_component(_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/.." ABSOLUTE)
  set(ASTER_DISTRIBUTION_CONFIG "${_root}/config/distribution.json" CACHE FILEPATH "First-boot source URL and public TLS CA bundle configuration")
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(_generated "${CMAKE_CURRENT_BINARY_DIR}/distribution")
  execute_process(COMMAND "${Python3_EXECUTABLE}" "${_root}/tools/configure_distribution.py"
    "${ASTER_DISTRIBUTION_CONFIG}" "${_generated}"
    RESULT_VARIABLE _result OUTPUT_VARIABLE _certificate OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT _result EQUAL 0)
    message(FATAL_ERROR "Invalid Aster distribution configuration")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${ASTER_DISTRIBUTION_CONFIG}" "${_certificate}" "${_root}/tools/configure_distribution.py")
  target_include_directories(${target} PUBLIC "${_generated}")
endfunction()
