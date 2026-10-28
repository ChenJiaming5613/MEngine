# Script mode, incremental: file(COPY) skips files whose destination already has the same timestamp.
#
#   cmake -DMODE=STAGE -DASSET_SOURCE_DIR=... -DSCENE_SOURCE_DIR=... -DSCENE_DIR=... -DDESTINATION_DIR=... -P CopyAssets.cmake
#   cmake -DMODE=MIRROR -DSOURCE_DIR=... -DDESTINATION_DIR=... -P CopyAssets.cmake
#   cmake -DMODE=PRUNE_SHADERS -DDESTINATION_DIR=... -DKEEP_SHADERS=a.spv|b.spv -P CopyAssets.cmake
#
# PRUNE_SHADERS deletes <dir>/shaders/*.spv not named in KEEP_SHADERS; MIRROR deletes destination shaders
# that no longer exist in the source.
#
# <dir>/models/.scene records which scene directory models/ holds. When it changes, models/ is removed first
# so a previously staged scene never lingers. Dot files are not packaged by AAPT.

# Script mode starts without policies, if(IN_LIST) needs CMP0057.
cmake_minimum_required(VERSION 3.22)

function(MEngineDropStaleModels DESTINATION NEW_SCENE_DIR)
    set(STAMP_FILE "${DESTINATION}/models/.scene")
    set(OLD_SCENE_DIR "")
    if(EXISTS "${STAMP_FILE}")
        file(READ "${STAMP_FILE}" OLD_SCENE_DIR)
    endif()
    if(NOT OLD_SCENE_DIR STREQUAL NEW_SCENE_DIR)
        if(EXISTS "${DESTINATION}/models")
            message(STATUS "Removing stale models/ (was \"${OLD_SCENE_DIR}\", now \"${NEW_SCENE_DIR}\")")
            file(REMOVE_RECURSE "${DESTINATION}/models")
        endif()
    endif()
endfunction()

# MEnginePruneShaders(<dir> <file names to keep>)
function(MEnginePruneShaders SHADER_DIR KEEP_NAMES)
    file(GLOB STAGED_SHADERS "${SHADER_DIR}/*.spv")
    foreach(STAGED_SHADER IN LISTS STAGED_SHADERS)
        get_filename_component(STAGED_NAME "${STAGED_SHADER}" NAME)
        if(NOT STAGED_NAME IN_LIST KEEP_NAMES)
            message(STATUS "Removing stale shader ${STAGED_NAME}")
            file(REMOVE "${STAGED_SHADER}")
        endif()
    endforeach()
endfunction()

if(NOT DEFINED DESTINATION_DIR OR DESTINATION_DIR STREQUAL "")
    message(FATAL_ERROR "CopyAssets.cmake: DESTINATION_DIR is required")
endif()

if(MODE STREQUAL "STAGE")
    MEngineDropStaleModels("${DESTINATION_DIR}" "${SCENE_DIR}")

    file(MAKE_DIRECTORY "${DESTINATION_DIR}")
    file(COPY "${ASSET_SOURCE_DIR}/" DESTINATION "${DESTINATION_DIR}")

    if(NOT SCENE_DIR STREQUAL "")
        file(COPY "${SCENE_SOURCE_DIR}/" DESTINATION "${DESTINATION_DIR}/models/${SCENE_DIR}")
        set(STAMP_FILE "${DESTINATION_DIR}/models/.scene")
        set(CURRENT_STAMP "")
        if(EXISTS "${STAMP_FILE}")
            file(READ "${STAMP_FILE}" CURRENT_STAMP)
        endif()
        if(NOT CURRENT_STAMP STREQUAL SCENE_DIR)
            file(WRITE "${STAMP_FILE}" "${SCENE_DIR}")
        endif()
    endif()
elseif(MODE STREQUAL "MIRROR")
    set(SOURCE_SCENE_DIR "")
    if(EXISTS "${SOURCE_DIR}/models/.scene")
        file(READ "${SOURCE_DIR}/models/.scene" SOURCE_SCENE_DIR)
    endif()
    MEngineDropStaleModels("${DESTINATION_DIR}" "${SOURCE_SCENE_DIR}")

    file(MAKE_DIRECTORY "${DESTINATION_DIR}")
    if(EXISTS "${SOURCE_DIR}")
        file(COPY "${SOURCE_DIR}/" DESTINATION "${DESTINATION_DIR}")

        file(GLOB SOURCE_SHADERS "${SOURCE_DIR}/shaders/*.spv")
        set(SOURCE_SHADER_NAMES)
        foreach(SOURCE_SHADER IN LISTS SOURCE_SHADERS)
            get_filename_component(SOURCE_SHADER_NAME "${SOURCE_SHADER}" NAME)
            list(APPEND SOURCE_SHADER_NAMES "${SOURCE_SHADER_NAME}")
        endforeach()
        MEnginePruneShaders("${DESTINATION_DIR}/shaders" "${SOURCE_SHADER_NAMES}")
    endif()
elseif(MODE STREQUAL "PRUNE_SHADERS")
    string(REPLACE "|" ";" KEEP_SHADER_NAMES "${KEEP_SHADERS}")
    MEnginePruneShaders("${DESTINATION_DIR}/shaders" "${KEEP_SHADER_NAMES}")
else()
    message(FATAL_ERROR "CopyAssets.cmake: unknown MODE \"${MODE}\"")
endif()
