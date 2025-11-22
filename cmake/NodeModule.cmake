include_guard(GLOBAL)

function(NodeModule_Find name)
  execute_process(
    COMMAND node --print "require('${name}').include_dir"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE node_module_include_dir
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
  )

  add_library("${name}" INTERFACE)
  target_include_directories("${name}" INTERFACE "${node_module_include_dir}")
endfunction()
