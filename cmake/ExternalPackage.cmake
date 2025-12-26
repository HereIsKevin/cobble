include_guard(GLOBAL)

include(ExternalProject)
include(ProcessorCount)

if(NOT DEFINED EXTERNAL_PACKAGE_PARALLEL_LEVEL)
  ProcessorCount(EXTERNAL_PACKAGE_PARALLEL_LEVEL)
endif()

if(NOT DEFINED EXTERNAL_PACKAGE_PROPAGATED_VARIABLES)
  set(
    EXTERNAL_PACKAGE_PROPAGATED_VARIABLES
    CMAKE_BUILD_TYPE
    CMAKE_INTERPROCEDURAL_OPTIMIZATION
    CMAKE_OSX_DEPLOYMENT_TARGET
  )
endif()

function(ExternalPackage_Add name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "TEST" "URL;HASH" "CMAKE_ARGS")

  if(arg_TEST)
    set(
      test_command
      TEST_COMMAND
        "${CMAKE_CTEST_COMMAND}"
        --parallel "${EXTERNAL_PACKAGE_PARALLEL_LEVEL}"
    )
  endif()

  foreach(variable IN LISTS EXTERNAL_PACKAGE_PROPAGATED_VARIABLES)
    if(DEFINED "${variable}")
      list(APPEND cmake_args -D "${variable}=${${variable}}")
    endif()
  endforeach()

  ExternalProject_Add(
    "${name}"
    PREFIX "${CMAKE_BINARY_DIR}/deps/${name}/"
    URL "${arg_URL}"
    URL_HASH "SHA256=${arg_HASH}"
    DOWNLOAD_NO_PROGRESS TRUE
    TLS_VERIFY TRUE
    CMAKE_ARGS
      -D CMAKE_POSITION_INDEPENDENT_CODE=TRUE
      -D CMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      ${cmake_args}
      ${arg_CMAKE_ARGS}
    UPDATE_COMMAND ""
    ${test_command}
    DEPENDS ${_EXTERNAL_PROJECT_TARGETS}
  )

  list(APPEND _EXTERNAL_PROJECT_TARGETS "${name}")
  set(_EXTERNAL_PROJECT_TARGETS ${_EXTERNAL_PROJECT_TARGETS} PARENT_SCOPE)
endfunction()

function(ExternalPackage_Find name)
  find_package(
    "${name}"
    REQUIRED
    CONFIG
    PATHS "${CMAKE_BINARY_DIR}/deps/${name}/"
    NO_DEFAULT_PATH
  )
endfunction()

function(ExternalPackage_Get_Directory name variable)
  set("${variable}" "${CMAKE_BINARY_DIR}/deps/${name}/")
  return(PROPAGATE "${variable}")
endfunction()
