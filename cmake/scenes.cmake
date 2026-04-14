
define_property(TARGET PROPERTY BAKER_USE_BEST_FIT_NORMALS
  BRIEF_DOCS "Use best fit normals when baking (-bfn)"
  FULL_DOCS  "When set to ON, passes -bfn to the baker executable"
)
define_property(TARGET PROPERTY BAKER_EMBED_IMAGES
  BRIEF_DOCS "Embed images into baked output (-embedImg)"
  FULL_DOCS  "When set to ON, passes -embedImg to the baker executable"
)
define_property(TARGET PROPERTY BAKER_EMBED_BUFFERS
  BRIEF_DOCS "Embed buffers into baked output (-embedBuf)"
  FULL_DOCS  "When set to ON, passes -embedBuf to the baker executable"
)

# Set defaults on the baker target itself -- change these as needed
set_target_properties(baker PROPERTIES
  BAKER_USE_BEST_FIT_NORMALS OFF
  BAKER_EMBED_IMAGES         OFF
  BAKER_EMBED_BUFFERS        OFF
)

set(BAKER_EXTRA_ARGS "")
get_target_property(_bfn baker BAKER_USE_BEST_FIT_NORMALS)
get_target_property(_ei  baker BAKER_EMBED_IMAGES)
get_target_property(_eb  baker BAKER_EMBED_BUFFERS)
if(_bfn)
  list(APPEND BAKER_EXTRA_ARGS -bfn)
endif()
if(_ei)
  list(APPEND BAKER_EXTRA_ARGS -embedImg)
endif()
if(_eb)
  list(APPEND BAKER_EXTRA_ARGS -embedBuf)
endif()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/resources/scenes"
)
file(GLOB SCENE_GLTF_FILES
  "${PROJECT_SOURCE_DIR}/resources/scenes/*/*.gltf"
)

set(BAKED_OUTPUTS "")

foreach(GLTF_FILE IN LISTS SCENE_GLTF_FILES)
  get_filename_component(SCENE_DIR  "${GLTF_FILE}" DIRECTORY)
  get_filename_component(SCENE_NAME "${GLTF_FILE}" NAME_WE)
  get_filename_component(SCENE_SUBDIR "${SCENE_DIR}" NAME)

  file(GLOB_RECURSE SCENE_DEPS "${SCENE_DIR}/*")

  set(BAKED_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/baked_scenes/${SCENE_SUBDIR}")
  set(BAKED_OUTPUT "${BAKED_OUTPUT_DIR}/${SCENE_NAME}.gltf")

  add_custom_command(
    OUTPUT  "${BAKED_OUTPUT}"
    COMMAND ${CMAKE_COMMAND} -E make_directory ${BAKED_OUTPUT_DIR}
    COMMAND $<TARGET_FILE:baker> "${GLTF_FILE}" ${BAKER_EXTRA_ARGS} -o "${BAKED_OUTPUT}"
    DEPENDS baker ${SCENE_DEPS}
    COMMENT "Baking scene: ${SCENE_SUBDIR}"
    VERBATIM
  )

  list(APPEND BAKED_OUTPUTS "${BAKED_OUTPUT}")
endforeach()

add_custom_target(bake_all_scenes ALL
  DEPENDS ${BAKED_OUTPUTS}
)
