include_guard(GLOBAL)

include(ExternalProject)
include(ProcessorCount)

if(NOT DEFINED EXTERNAL_PACKAGE_PARALLEL_LEVEL)
    ProcessorCount(EXTERNAL_PACKAGE_PARALLEL_LEVEL)
endif()

function(ExternalPackage_Start)
    if(NOT EXTERNAL_PACKAGE_BUILD)
        execute_process(
            COMMAND
                "${CMAKE_COMMAND}"
                -G "${CMAKE_GENERATOR}"
                -D "CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
                -D "EXTERNAL_PACKAGE_BUILD=TRUE"
                "${CMAKE_SOURCE_DIR}"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMAND_ERROR_IS_FATAL ANY
        )

        execute_process(
            COMMAND
                "${CMAKE_COMMAND}"
                --build ./
                --parallel "${EXTERNAL_PACKAGE_PARALLEL_LEVEL}"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMAND_ERROR_IS_FATAL ANY
        )
    endif()
endfunction()

function(ExternalPackage_Add name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "TEST" "URL;HASH" "CMAKE_ARGS")

    if(NOT EXTERNAL_PACKAGE_BUILD)
        return()
    endif()

    if(arg_TEST)
        set(
            test_command
            TEST_COMMAND
                "${CMAKE_CTEST_COMMAND}"
                --parallel "${EXTERNAL_PACKAGE_PARALLEL_LEVEL}"
        )
    endif()

    set(prefix "${CMAKE_BINARY_DIR}/deps/${name}/")

    ExternalProject_Add(
        "${name}"
        PREFIX "${prefix}"
        URL "${arg_URL}"
        URL_HASH "SHA256=${arg_HASH}"
        DOWNLOAD_NO_PROGRESS TRUE
        TLS_VERIFY TRUE
        CMAKE_ARGS
            -D "CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}"
            -D CMAKE_INSTALL_PREFIX=<INSTALL_DIR>
            -D "CMAKE_PREFIX_PATH=${_EXTERNAL_PROJECT_FIND_PATHS}"
            ${arg_CMAKE_ARGS}
        UPDATE_COMMAND ""
        ${test_command}
        DEPENDS ${_EXTERNAL_PROJECT_TARGETS}
    )

    list(APPEND _EXTERNAL_PROJECT_TARGETS "${name}")
    set(_EXTERNAL_PROJECT_TARGETS ${_EXTERNAL_PROJECT_TARGETS} PARENT_SCOPE)

    list(APPEND _EXTERNAL_PROJECT_FIND_PATHS "${prefix}")
    set(
        _EXTERNAL_PROJECT_FIND_PATHS
        ${_EXTERNAL_PROJECT_FIND_PATHS}
        PARENT_SCOPE
    )
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

macro(ExternalPackage_End)
    if(EXTERNAL_PACKAGE_BUILD)
        return()
    endif()
endmacro()
