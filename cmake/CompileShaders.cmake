# slangc ships with the Vulkan SDK since 1.3.296. It has no NDK counterpart, so the Android build needs it on the host too.
find_program(MENGINE_SLANGC
    NAMES slangc slangc.exe
    HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "$ENV{VULKAN_SDK}/bin"
    DOC "The Slang compiler"
)

if(NOT MENGINE_SLANGC)
    message(FATAL_ERROR "slangc was not found. Install the Vulkan SDK 1.3.296+ (with VULKAN_SDK set) or pass -DMENGINE_SLANGC=/path/to/slangc.")
endif()

# Vulkan 1.1 accepts SPIR-V up to 1.3. Only -profile selects the version, -capability does not.
set(MENGINE_SLANG_PROFILE spirv_1_3)

# E50011 "SPIR-V version too old" is a blanket notice for any profile below slangc's default.
set(MENGINE_SLANG_SUPPRESSED_WARNINGS 50011 CACHE STRING "slangc warning ids to suppress")

set(MENGINE_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/source/shaders")

set(MENGINE_SLANG_WARNING_ARGUMENTS)
if(MENGINE_SLANG_SUPPRESSED_WARNINGS)
    list(JOIN MENGINE_SLANG_SUPPRESSED_WARNINGS "," MENGINE_SLANG_WARNING_LIST)
    set(MENGINE_SLANG_WARNING_ARGUMENTS -warnings-disable "${MENGINE_SLANG_WARNING_LIST}")
endif()

# MEngineCompileShader(<slang-source> <entry-point>)
# Writes <staged assets>/shaders/<Name>.<EntryPoint>.spv, loaded at runtime as "shaders/<Name>.<EntryPoint>.spv".
function(MEngineCompileShader SHADER_SOURCE ENTRY_POINT)
    get_filename_component(SHADER_NAME "${SHADER_SOURCE}" NAME_WE)
    get_filename_component(SHADER_ABSOLUTE "${SHADER_SOURCE}" ABSOLUTE)

    set(OUTPUT_DIRECTORY "${MENGINE_STAGED_ASSET_DIR}/shaders")
    set(SPIRV_OUTPUT "${OUTPUT_DIRECTORY}/${SHADER_NAME}.${ENTRY_POINT}.spv")

    file(GLOB COMMON_SHADERS CONFIGURE_DEPENDS "${MENGINE_SHADER_SOURCE_DIR}/common/*.slang")

    add_custom_command(
        OUTPUT "${SPIRV_OUTPUT}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${OUTPUT_DIRECTORY}"
        COMMAND "${MENGINE_SLANGC}" "${SHADER_ABSOLUTE}"
            -target spirv
            -entry "${ENTRY_POINT}"
            -profile "${MENGINE_SLANG_PROFILE}"
            -I "${MENGINE_SHADER_SOURCE_DIR}"
            ${MENGINE_SLANG_WARNING_ARGUMENTS}
            -O2
            -o "${SPIRV_OUTPUT}"
        DEPENDS "${SHADER_ABSOLUTE}" ${COMMON_SHADERS}
        COMMENT "Compiling ${SHADER_NAME}.slang:${ENTRY_POINT} to SPIR-V"
        VERBATIM
    )

    set_property(GLOBAL APPEND PROPERTY MENGINE_SHADER_OUTPUTS "${SPIRV_OUTPUT}")
endfunction()

# Must be called from the same directory as the MEngineCompileShader calls (custom command outputs are directory scoped).
function(MEngineAddShaderTarget TARGET_NAME)
    get_property(SHADER_OUTPUTS GLOBAL PROPERTY MENGINE_SHADER_OUTPUTS)

    # Removes .spv files of shaders that are no longer compiled, so renamed or deleted shaders are not packaged.
    set(SHADER_FILE_NAMES)
    foreach(SHADER_OUTPUT IN LISTS SHADER_OUTPUTS)
        get_filename_component(SHADER_FILE_NAME "${SHADER_OUTPUT}" NAME)
        list(APPEND SHADER_FILE_NAMES "${SHADER_FILE_NAME}")
    endforeach()
    list(JOIN SHADER_FILE_NAMES "|" SHADER_KEEP_LIST)

    add_custom_target(${TARGET_NAME} ALL
        COMMAND "${CMAKE_COMMAND}"
            -DMODE=PRUNE_SHADERS
            "-DDESTINATION_DIR=${MENGINE_STAGED_ASSET_DIR}"
            "-DKEEP_SHADERS=${SHADER_KEEP_LIST}"
            -P "${MENGINE_COPY_ASSETS_SCRIPT}"
        DEPENDS ${SHADER_OUTPUTS}
        VERBATIM
    )
endfunction()
