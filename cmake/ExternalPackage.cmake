include_guard(GLOBAL)

include(ExternalProject)
include(ProcessorCount)

if(NOT DEFINED EXTERNAL_PACKAGE_PARALLEL_LEVEL)
  ProcessorCount(EXTERNAL_PACKAGE_PARALLEL_LEVEL)
endif()

function(ExternalPackage_Add name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "TEST" "URL;HASH;EXPORT" "CMAKE_ARGS")

  if(arg_TEST)
    set(
      test_command
      TEST_COMMAND
        "${CMAKE_CTEST_COMMAND}"
        --parallel "${EXTERNAL_PACKAGE_PARALLEL_LEVEL}"
    )
  endif()

  ExternalProject_Add(
    "${name}"
    PREFIX "${CMAKE_BINARY_DIR}/deps/${name}/"
    URL "${arg_URL}"
    URL_HASH "SHA256=${arg_HASH}"
    DOWNLOAD_NO_PROGRESS TRUE
    TLS_VERIFY TRUE
    CMAKE_ARGS
      -D "CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
      -D "CMAKE_INTERPROCEDURAL_OPTIMIZATION=${CMAKE_INTERPROCEDURAL_OPTIMIZATION}"
      -D "CMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}"
      -D "CMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
      -D CMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      ${arg_CMAKE_ARGS}
    UPDATE_COMMAND ""
    ${test_command}
    DEPENDS ${_EXTERNAL_PROJECT_TARGETS}
  )

  list(APPEND _EXTERNAL_PROJECT_TARGETS "${name}")
  set(_EXTERNAL_PROJECT_TARGETS ${_EXTERNAL_PROJECT_TARGETS} PARENT_SCOPE)

  if(arg_EXPORT)
    ExternalProject_Get_Property("${name}" INSTALL_DIR)
    set("${arg_EXPORT}" "${INSTALL_DIR}")
    return(PROPAGATE "${arg_EXPORT}")
  endif()
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
