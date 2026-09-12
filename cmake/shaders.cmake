
# NOTE: We cannot do transitive properties before cmake 3.30, sadly
define_property(TARGET PROPERTY SHADER_INCLUDE_DIRECTORIES
  BRIEF_DOCS "Include directories for shader compilation"
  FULL_DOCS "Adds these include directories to all shaders of this target"
)

# But we can manually define and grab interface properties
define_property(TARGET PROPERTY INTERFACE_SHADER_INCLUDE_DIRECTORIES
  BRIEF_DOCS "Include directories for shader compilation"
  FULL_DOCS "Adds this include directories to all shaders of targets that depend on this one"
)

find_program(glslang_validator glslangValidator)
find_program(spirv_opt spirv-opt)

# Wokrs same way as target_include_directories, i.e. PUBLIC/PRIVATE/INTERFACE are supported
function(target_shader_include_directories tgt)
  list(POP_FRONT ${ARGN})
  set(current_type "PUBLIC")
  foreach(arg ${ARGN})
    if("${arg}" STREQUAL "INTERFACE" OR "${arg}" STREQUAL "PUBLIC" OR "${arg}" STREQUAL "PRIVATE")
      set(current_type "${arg}")
      continue()
    endif()

    set(abs_path "$<PATH:ABSOLUTE_PATH,NORMALIZE,$<TARGET_GENEX_EVAL:${tgt},${arg}>,$<TARGET_PROPERTY:${tgt},SOURCE_DIR>>")

    if(${current_type} STREQUAL "PUBLIC")
      set_property(TARGET ${tgt} APPEND PROPERTY SHADER_INCLUDE_DIRECTORIES ${abs_path})
      set_property(TARGET ${tgt} APPEND PROPERTY INTERFACE_SHADER_INCLUDE_DIRECTORIES ${abs_path})
    elseif(${current_type} STREQUAL "PRIVATE")
      set_property(TARGET ${tgt} APPEND PROPERTY SHADER_INCLUDE_DIRECTORIES ${abs_path})
    elseif(${current_type} STREQUAL "INTERFACE")
      set_property(TARGET ${tgt} APPEND PROPERTY INTERFACE_SHADER_INCLUDE_DIRECTORIES ${abs_path})
    endif()
  endforeach(arg)
endfunction()

function(target_add_shaders tgt)
  list(POP_FRONT ${ARGN})

  set(shader_binaries_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders/")

  set(incl_dirs "$<TARGET_GENEX_EVAL:${tgt},$<TARGET_PROPERTY:${tgt},SHADER_INCLUDE_DIRECTORIES>>")

  foreach(glsl_path ${ARGN})
    set(input_path "${CMAKE_CURRENT_LIST_DIR}/${glsl_path}")
    set(output_path "${shader_binaries_dir}/$<PATH:GET_FILENAME,${glsl_path}>.spv")
    set(unoptimized_output_path "${output_path}.unopt.spv")
    add_custom_command(
      OUTPUT ${output_path}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${shader_binaries_dir}
      COMMAND ${glslang_validator}
        "$<$<BOOL:${incl_dirs}>:-I$<JOIN:${incl_dirs},;-I>>"
        "$<$<CONFIG:Debug>:-g>"
        -V
        ${input_path}
        -o ${unoptimized_output_path}
        --depfile "${output_path}.d"
      COMMAND ${spirv_opt}
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:-O>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:--eliminate-dead-branches>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:--merge-blocks>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:--eliminate-dead-code-aggressive>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:--inline-entry-points-exhaustive>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:--eliminate-dead-functions>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:--scalar-replacement=100>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:--loop-unroll>"
        "$<$<OR:$<CONFIG:Release>,$<CONFIG:MinSizeRel>>:--strip-debug>"
        "--preserve-bindings"
        "--preserve-spec-constants"
        ${unoptimized_output_path} -o ${output_path}
      VERBATIM
      COMMAND_EXPAND_LISTS
      DEPENDS ${input_path}
      DEPFILE "${output_path}.d"
    )
    list(APPEND SPIRV_BINARY_FILES ${output_path})
  endforeach(glsl_path)

  set(custom_target_name "${tgt}_shaders")

  if(TARGET ${custom_target_name})
    message(FATAL_ERROR "Sorry, you can't call target_add_shaders multiple times cuz it's unimplemented. Fell free to create a PR.")
  else()
    set_target_properties(${tgt} PROPERTIES
      TRANSITIVE_COMPILE_PROPERTIES "SHADER_INCLUDE_DIRECTORIES"
    )

    add_custom_target(${custom_target_name} DEPENDS ${SPIRV_BINARY_FILES})
    add_dependencies(${tgt} ${custom_target_name})
    add_compile_definitions(${tgt}
      PRIVATE $<UPPER_CASE:${tgt}>_SHADERS_ROOT="${shader_binaries_dir}")
  endif()
endfunction()

function(target_add_shaders_slang tgt)
  list(LENGTH ARGN len)

  set(shader_binaries_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders/")

  set(incl_dirs "$<TARGET_GENEX_EVAL:${tgt},$<TARGET_PROPERTY:${tgt},SHADER_INCLUDE_DIRECTORIES>>")

  set(i 0)
  while(i LESS len)
    list(GET ARGN ${i} directive)
    if(NOT directive STREQUAL "SHADER")
      message(FATAL_ERROR "Expected SHADER directive, got ${directive}")
    endif()

    math(EXPR i "${i} + 1")
    if(NOT i LESS len)
      message(FATAL_ERROR "Expected shader file path")
    endif()

    list(GET ARGN ${i} path)
    if(path STREQUAL "SHADER")
      message(FATAL_ERROR "Expected shader file path")
    endif()

    math(EXPR i "${i} + 1")
    if(NOT i LESS len)
      message(FATAL_ERROR "Expected shader entry point")
    endif()

    set(entry_args "")
    set(entry_suffix "")
    list(GET ARGN ${i} entry)
    while(i LESS len AND NOT entry STREQUAL "SHADER")
      list(APPEND entry_args "-e" ${entry})
      string(APPEND entry_suffix "-${entry}")
      math(EXPR i "${i} + 1")
      if(i LESS len)
        list(GET ARGN ${i} entry)
      endif()
    endwhile()

    if(entry_suffix STREQUAL "")
      list(APPEND entry_args "-e" "main")
      string(APPEND entry_suffix "-main")
    endif()

    set(input_path "${CMAKE_CURRENT_LIST_DIR}/${path}")
    set(output_path "${shader_binaries_dir}/$<PATH:GET_FILENAME,${path}>${entry_suffix}.escb")
    add_custom_command(
      OUTPUT ${output_path}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${shader_binaries_dir}
      COMMAND $<TARGET_FILE:etna-slangc>
        "$<$<BOOL:${incl_dirs}>:-I;$<LIST:JOIN,${incl_dirs},;-I;>>"
        "$<$<CONFIG:Debug>:-g>"
        ${input_path}
        -o ${output_path}
        ${entry_args}
        -df "${output_path}.d"
      VERBATIM
      COMMAND_EXPAND_LISTS
      DEPENDS ${input_path}
      DEPENDS etna-slangc
      DEPFILE "${output_path}.d"
    )

    list(APPEND SHADER_BINARY_FILES ${output_path})
  endwhile()

  set(custom_target_name "${tgt}_slang_shaders")

  if(TARGET ${custom_target_name})
    message(FATAL_ERROR "Sorry, you can't call target_add_shaders_slang multiple times cuz it's unimplemented. Fell free to create a PR.")
  else()
    set_target_properties(${tgt} PROPERTIES
      TRANSITIVE_COMPILE_PROPERTIES "SHADER_INCLUDE_DIRECTORIES"
    )

    add_custom_target(${custom_target_name} DEPENDS ${SHADER_BINARY_FILES})
    add_dependencies(${tgt} ${custom_target_name})
    add_compile_definitions(${tgt}
      PRIVATE $<UPPER_CASE:${tgt}>_SLANG_SHADERS_ROOT="${shader_binaries_dir}")
  endif()
endfunction()
