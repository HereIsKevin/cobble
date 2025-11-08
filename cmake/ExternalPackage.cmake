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
    if(NOT EXTERNAL_PACKAGE_BUILD)
        find_package(
            "${name}"
            REQUIRED
            CONFIG
            PATHS "${CMAKE_BINARY_DIR}/deps/${name}/"
            NO_DEFAULT_PATH
        )

        return()
    endif()

    cmake_parse_arguments(PARSE_ARGV 1 arg "TEST" "URL;HASH" "CMAKE_ARGS")

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
            -D CMAKE_INSTALL_PREFIX=<INSTALL_DIR>
            ${arg_CMAKE_ARGS}
        UPDATE_COMMAND ""
        ${test_command}
        DEPENDS ${EXTERNAL_PROJECT_TARGETS}
    )

    list(APPEND EXTERNAL_PROJECT_TARGETS "${name}")
    set(EXTERNAL_PROJECT_TARGETS ${EXTERNAL_PROJECT_TARGETS} PARENT_SCOPE)
endfunction()

macro(ExternalPackage_End)
    if(EXTERNAL_PACKAGE_BUILD)
        return()
    endif()
endmacro()
