# Script mode, incremental: file(COPY) skips files whose destination already has the same timestamp.
#
#   cmake -DMODE=STAGE -DASSET_SOURCE_DIR=... -DSCENE_SOURCE_ROOT=... -DSCENE_DIRS=a/b|c/d -DDESTINATION_DIR=... -P CopyAssets.cmake
#   cmake -DMODE=MIRROR -DSOURCE_DIR=... -DDESTINATION_DIR=... -P CopyAssets.cmake
#   cmake -DMODE=PRUNE_SHADERS -DDESTINATION_DIR=... -DKEEP_SHADERS=a.spv|b.spv -P CopyAssets.cmake
#
# STAGE copies assets/ to <dir>/ and every <SCENE_SOURCE_ROOT>/<scene dir> to <dir>/models/<scene dir>. It deletes
# staged files and directories that no longer exist in assets/ (shaders/ is managed by PRUNE_SHADERS), and anything in
# models/ that is outside the configured scene directories or no longer exists in their sources. MIRROR deletes
# everything that no longer exists in the staged directory. Removed scenes or assets therefore never linger in the
# build or the APK.

# Script mode starts without policies, if(IN_LIST) needs CMP0057.
cmake_minimum_required(VERSION 3.22)

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

# MEnginePruneMissing(<source dir> <destination dir> [top level names to keep...])
function(MEnginePruneMissing SOURCE DESTINATION)
    set(KEEP_TOP_LEVEL ${ARGN})
    file(GLOB_RECURSE DESTINATION_ENTRIES LIST_DIRECTORIES true RELATIVE "${DESTINATION}" "${DESTINATION}/*")
    # Sorted, so a removed directory comes before its contents; EXISTS then skips the contents.
    list(SORT DESTINATION_ENTRIES)
    foreach(ENTRY IN LISTS DESTINATION_ENTRIES)
        string(REGEX REPLACE "/.*$" "" TOP_LEVEL "${ENTRY}")
        if(TOP_LEVEL IN_LIST KEEP_TOP_LEVEL OR NOT EXISTS "${DESTINATION}/${ENTRY}")
            continue()
        endif()
        if(NOT EXISTS "${SOURCE}/${ENTRY}")
            message(STATUS "Removing stale ${ENTRY}")
            file(REMOVE_RECURSE "${DESTINATION}/${ENTRY}")
        endif()
    endforeach()
endfunction()

# MEnginePruneModels(<models dir> <scene source root> <scene dirs>): keeps the parents of each scene directory and the
# entries inside one that still exist in its source; removes everything else.
function(MEnginePruneModels MODELS_DIR SOURCE_ROOT SCENE_DIRS)
    if(NOT EXISTS "${MODELS_DIR}")
        return()
    endif()
    file(GLOB_RECURSE MODEL_ENTRIES LIST_DIRECTORIES true RELATIVE "${MODELS_DIR}" "${MODELS_DIR}/*")
    list(SORT MODEL_ENTRIES)
    foreach(ENTRY IN LISTS MODEL_ENTRIES)
        if(NOT EXISTS "${MODELS_DIR}/${ENTRY}")
            continue()
        endif()
        set(KEEP FALSE)
        foreach(SCENE_DIR IN LISTS SCENE_DIRS)
            string(FIND "${SCENE_DIR}/" "${ENTRY}/" PARENT_POSITION)
            string(FIND "${ENTRY}/" "${SCENE_DIR}/" INSIDE_POSITION)
            if(PARENT_POSITION EQUAL 0 OR (INSIDE_POSITION EQUAL 0 AND EXISTS "${SOURCE_ROOT}/${ENTRY}"))
                set(KEEP TRUE)
                break()
            endif()
        endforeach()
        if(NOT KEEP)
            message(STATUS "Removing stale models/${ENTRY}")
            file(REMOVE_RECURSE "${MODELS_DIR}/${ENTRY}")
        endif()
    endforeach()
endfunction()

if(NOT DEFINED DESTINATION_DIR OR DESTINATION_DIR STREQUAL "")
    message(FATAL_ERROR "CopyAssets.cmake: DESTINATION_DIR is required")
endif()

if(MODE STREQUAL "STAGE")
    string(REPLACE "|" ";" SCENE_DIR_LIST "${SCENE_DIRS}")

    file(MAKE_DIRECTORY "${DESTINATION_DIR}")
    MEnginePruneMissing("${ASSET_SOURCE_DIR}" "${DESTINATION_DIR}" models shaders)
    MEnginePruneModels("${DESTINATION_DIR}/models" "${SCENE_SOURCE_ROOT}" "${SCENE_DIR_LIST}")
    file(COPY "${ASSET_SOURCE_DIR}/" DESTINATION "${DESTINATION_DIR}")

    foreach(SCENE_DIR IN LISTS SCENE_DIR_LIST)
        file(COPY "${SCENE_SOURCE_ROOT}/${SCENE_DIR}/" DESTINATION "${DESTINATION_DIR}/models/${SCENE_DIR}")
    endforeach()
elseif(MODE STREQUAL "MIRROR")
    file(MAKE_DIRECTORY "${DESTINATION_DIR}")
    if(EXISTS "${SOURCE_DIR}")
        MEnginePruneMissing("${SOURCE_DIR}" "${DESTINATION_DIR}")
        file(COPY "${SOURCE_DIR}/" DESTINATION "${DESTINATION_DIR}")
    endif()
elseif(MODE STREQUAL "PRUNE_SHADERS")
    string(REPLACE "|" ";" KEEP_SHADER_NAMES "${KEEP_SHADERS}")
    MEnginePruneShaders("${DESTINATION_DIR}/shaders" "${KEEP_SHADER_NAMES}")
else()
    message(FATAL_ERROR "CopyAssets.cmake: unknown MODE \"${MODE}\"")
endif()
