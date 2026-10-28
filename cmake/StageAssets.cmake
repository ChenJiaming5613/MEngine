# Builds the staged asset directory that is shipped with the application:
#   <staged>/                 contents of assets/ (config.json, textures, environments)
#   <staged>/models/<dir>/    for every config.json "scenes" entry, the whole directory of its glTF file
#   <staged>/shaders/*.spv    written by MEngineCompileShader
# Desktop mirrors it next to the executable; Android uses it as the Gradle assets.srcDir.

set(MENGINE_STAGED_ASSET_DIR "${CMAKE_BINARY_DIR}/staged_assets" CACHE PATH "Directory the runtime assets are staged into")
set(MENGINE_ASSET_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")
# Scene paths are relative to third_party/, e.g. glTF-Sample-Assets/Models/Sponza/glTF/Sponza.gltf.
set(MENGINE_SCENE_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(MENGINE_CONFIG_FILE "${MENGINE_ASSET_SOURCE_DIR}/config.json")
set(MENGINE_COPY_ASSETS_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/CopyAssets.cmake")

# Editing "scenes" must re-run the configure step, which recomputes the directories to copy.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${MENGINE_CONFIG_FILE}")

# Same rules as ViewerConfig at runtime: relative, no "..", must name an existing file inside a sub directory.
# Invalid or missing entries are skipped with a warning.
function(MEngineResolveSceneDirectories OUT_SCENE_DIRS)
    set(${OUT_SCENE_DIRS} "" PARENT_SCOPE)

    if(NOT EXISTS "${MENGINE_CONFIG_FILE}")
        message(WARNING "MEngine: ${MENGINE_CONFIG_FILE} does not exist, no scene is staged")
        return()
    endif()

    file(READ "${MENGINE_CONFIG_FILE}" CONFIG_JSON)
    string(JSON SCENE_COUNT ERROR_VARIABLE JSON_ERROR LENGTH "${CONFIG_JSON}" scenes)
    if(JSON_ERROR OR SCENE_COUNT EQUAL 0)
        message(WARNING "MEngine: ${MENGINE_CONFIG_FILE} has no usable \"scenes\" array (${JSON_ERROR}), no scene is staged")
        return()
    endif()

    set(SCENE_DIRS "")
    math(EXPR LAST_SCENE "${SCENE_COUNT} - 1")
    foreach(SCENE_INDEX RANGE ${LAST_SCENE})
        string(JSON SCENE_PATH ERROR_VARIABLE JSON_ERROR GET "${CONFIG_JSON}" scenes ${SCENE_INDEX} path)
        if(JSON_ERROR)
            message(WARNING "MEngine: scenes[${SCENE_INDEX}] has no \"path\" string, skipped")
            continue()
        endif()

        string(REPLACE "\\" "/" SCENE_PATH "${SCENE_PATH}")
        if(SCENE_PATH STREQUAL "" OR SCENE_PATH MATCHES "^/" OR SCENE_PATH MATCHES "^[A-Za-z]:")
            message(WARNING "MEngine: scene \"${SCENE_PATH}\" must be a relative path, skipped")
            continue()
        endif()
        if("/${SCENE_PATH}/" MATCHES "/\\.\\./")
            message(WARNING "MEngine: scene \"${SCENE_PATH}\" must not contain \"..\", skipped")
            continue()
        endif()

        set(SCENE_FILE "${MENGINE_SCENE_SOURCE_ROOT}/${SCENE_PATH}")
        if(NOT EXISTS "${SCENE_FILE}" OR IS_DIRECTORY "${SCENE_FILE}")
            message(WARNING "MEngine: scene file ${SCENE_FILE} does not exist (are the submodules initialized?), skipped")
            continue()
        endif()

        get_filename_component(SCENE_DIR "${SCENE_PATH}" DIRECTORY)
        if(SCENE_DIR STREQUAL "")
            message(WARNING "MEngine: scene \"${SCENE_PATH}\" must live in a sub directory of third_party/, skipped")
            continue()
        endif()
        list(APPEND SCENE_DIRS "${SCENE_DIR}")
    endforeach()

    list(REMOVE_DUPLICATES SCENE_DIRS)
    set(${OUT_SCENE_DIRS} "${SCENE_DIRS}" PARENT_SCOPE)
endfunction()

MEngineResolveSceneDirectories(MENGINE_SCENE_DIRS)
foreach(MENGINE_SCENE_DIR IN LISTS MENGINE_SCENE_DIRS)
    message(STATUS "MEngine: staging scene directory third_party/${MENGINE_SCENE_DIR}")
endforeach()
string(REPLACE ";" "|" MENGINE_SCENE_DIRS_ARGUMENT "${MENGINE_SCENE_DIRS}")

# No OUTPUT: runs on every build, the script itself skips files whose timestamps already match.
add_custom_target(MEngineStageAssets
    COMMAND "${CMAKE_COMMAND}"
        -DMODE=STAGE
        "-DASSET_SOURCE_DIR=${MENGINE_ASSET_SOURCE_DIR}"
        "-DSCENE_SOURCE_ROOT=${MENGINE_SCENE_SOURCE_ROOT}"
        "-DSCENE_DIRS=${MENGINE_SCENE_DIRS_ARGUMENT}"
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
