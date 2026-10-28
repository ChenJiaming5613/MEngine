# Builds the staged asset directory that is shipped with the application:
#   <staged>/                 contents of assets/ (config.json, textures, environments)
#   <staged>/models/<dir>/    the whole directory of the glTF file named by config.json "scene"
#   <staged>/shaders/*.spv    written by MEngineCompileShader
# Desktop mirrors it next to the executable; Android uses it as the Gradle assets.srcDir.

set(MENGINE_STAGED_ASSET_DIR "${CMAKE_BINARY_DIR}/staged_assets" CACHE PATH "Directory the runtime assets are staged into")
set(MENGINE_ASSET_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")
set(MENGINE_SAMPLE_MODELS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glTF-Sample-Assets/Models")
set(MENGINE_CONFIG_FILE "${MENGINE_ASSET_SOURCE_DIR}/config.json")
set(MENGINE_COPY_ASSETS_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/CopyAssets.cmake")

# Editing "scene" must re-run the configure step, which recomputes the directory to copy.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MENGINE_CONFIG_FILE}")

# Same rules as ViewerConfig at runtime: relative, no "..", must name an existing file inside a sub directory.
function(MEngineResolveSceneDirectory OUT_SCENE_DIR)
    set(${OUT_SCENE_DIR} "" PARENT_SCOPE)

    if(NOT EXISTS "${MENGINE_CONFIG_FILE}")
        message(WARNING "MEngine: ${MENGINE_CONFIG_FILE} does not exist, no scene is staged")
        return()
    endif()

    file(READ "${MENGINE_CONFIG_FILE}" CONFIG_JSON)
    string(JSON SCENE_PATH ERROR_VARIABLE JSON_ERROR GET "${CONFIG_JSON}" scene)
    if(JSON_ERROR)
        message(WARNING "MEngine: ${MENGINE_CONFIG_FILE} has no usable \"scene\" string (${JSON_ERROR}), no scene is staged")
        return()
    endif()

    string(REPLACE "\\" "/" SCENE_PATH "${SCENE_PATH}")
    if(SCENE_PATH STREQUAL "" OR SCENE_PATH MATCHES "^/" OR SCENE_PATH MATCHES "^[A-Za-z]:")
        message(WARNING "MEngine: scene \"${SCENE_PATH}\" must be a relative path, no scene is staged")
        return()
    endif()
    if("/${SCENE_PATH}/" MATCHES "/\\.\\./")
        message(WARNING "MEngine: scene \"${SCENE_PATH}\" must not contain \"..\", no scene is staged")
        return()
    endif()

    set(SCENE_FILE "${MENGINE_SAMPLE_MODELS_DIR}/${SCENE_PATH}")
    if(NOT EXISTS "${SCENE_FILE}" OR IS_DIRECTORY "${SCENE_FILE}")
        message(WARNING "MEngine: scene file ${SCENE_FILE} does not exist (is the glTF-Sample-Assets submodule initialized?), no scene is staged")
        return()
    endif()

    get_filename_component(SCENE_DIR "${SCENE_PATH}" DIRECTORY)
    if(SCENE_DIR STREQUAL "")
        message(WARNING "MEngine: scene \"${SCENE_PATH}\" must live in a sub directory of Models/, no scene is staged")
        return()
    endif()

    set(${OUT_SCENE_DIR} "${SCENE_DIR}" PARENT_SCOPE)
endfunction()

MEngineResolveSceneDirectory(MENGINE_SCENE_DIR)
if(MENGINE_SCENE_DIR)
    message(STATUS "MEngine: staging scene directory Models/${MENGINE_SCENE_DIR}")
endif()

# No OUTPUT: runs on every build, the script itself skips files whose timestamps already match.
add_custom_target(MEngineStageAssets
    COMMAND "${CMAKE_COMMAND}"
        -DMODE=STAGE
        "-DASSET_SOURCE_DIR=${MENGINE_ASSET_SOURCE_DIR}"
        "-DSCENE_SOURCE_DIR=${MENGINE_SAMPLE_MODELS_DIR}/${MENGINE_SCENE_DIR}"
        "-DSCENE_DIR=${MENGINE_SCENE_DIR}"
        "-DDESTINATION_DIR=${MENGINE_STAGED_ASSET_DIR}"
        -P "${MENGINE_COPY_ASSETS_SCRIPT}"
    COMMENT "Staging assets into ${MENGINE_STAGED_ASSET_DIR}"
    VERBATIM
)

# MEngineMirrorStagedAssets(<target>): copies the staged directory to <target dir>/assets after each build.
function(MEngineMirrorStagedAssets TARGET_NAME)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND "${CMAKE_COMMAND}"
            -DMODE=MIRROR
            "-DSOURCE_DIR=${MENGINE_STAGED_ASSET_DIR}"
            "-DDESTINATION_DIR=$<TARGET_FILE_DIR:${TARGET_NAME}>/assets"
            -P "${MENGINE_COPY_ASSETS_SCRIPT}"
        COMMENT "Mirroring staged assets next to the executable"
        VERBATIM
    )
endfunction()
