include_guard(GLOBAL)

function(NodeModule_Find name)
  cmake_parse_arguments(PARSE_ARGV 1 arg "DEF" "" "")

  execute_process(
    COMMAND node --print "require('${name}').include_dir"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE include_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
  )

  add_library("${name}" INTERFACE)
  target_include_directories("${name}" INTERFACE "${include_dir}")

  if(arg_DEF AND MSVC)
    execute_process(
      COMMAND
        node --print "Object.values(require('${name}').def_paths).join('\;')"
      WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
      OUTPUT_VARIABLE def_paths
      OUTPUT_STRIP_TRAILING_WHITESPACE
      COMMAND_ERROR_IS_FATAL ANY
    )

    foreach(def_path IN LISTS def_paths)
      cmake_path(GET def_path STEM def_name)
      set(lib_path "${CMAKE_BINARY_DIR}/deps/${name}/${def_name}.lib")
      set(lib_target "${name}_${def_name}")

      add_custom_command(
        OUTPUT "${lib_path}"
        COMMAND
          "${CMAKE_AR}"
          "/DEF:${def_path}"
          "/OUT:${lib_path}"
          ${CMAKE_STATIC_LINKER_FLAGS}
      )

      add_library("${lib_target}" SHARED IMPORTED)
      add_dependencies("${lib_target}" "${lib_path}")
      set_target_properties(
        "${lib_target}"
        PROPERTIES
          IMPORTED_IMPLIB "${lib_path}"
      )

      target_link_libraries("${name}" INTERFACE "${lib_target}")
    endforeach()
  endif()
endfunction()
