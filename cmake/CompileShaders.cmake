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

set(MENGINE_WRAP_SPIRV_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/WrapSpirv.cmake")

# Vulkan 1.1 accepts SPIR-V up to 1.3. Only -profile selects the version, -capability does not.
set(MENGINE_SLANG_PROFILE spirv_1_3)

# E50011 "SPIR-V version too old" is a blanket notice for any profile below slangc's default.
set(MENGINE_SLANG_SUPPRESSED_WARNINGS 50011 CACHE STRING "slangc warning ids to suppress")

# MEngineEmbedShader(<target> <slang-source> <entry-point> <c-array-name>)
function(MEngineEmbedShader TARGET_NAME SHADER_SOURCE ENTRY_POINT VARIABLE_NAME)
    get_filename_component(SHADER_NAME "${SHADER_SOURCE}" NAME_WE)
    get_filename_component(SHADER_ABSOLUTE "${SHADER_SOURCE}" ABSOLUTE)

    set(GENERATED_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/shaders")

    # slangc picks the embed output from the file extension and writes nothing for an unknown one, so both must end in .h.
    set(SPIRV_EMBEDDED "${GENERATED_DIRECTORY}/${SHADER_NAME}.${ENTRY_POINT}.embed.h")
    set(SPIRV_HEADER "${GENERATED_DIRECTORY}/${SHADER_NAME}.${ENTRY_POINT}.h")

    set(WARNING_ARGUMENTS)
    if(MENGINE_SLANG_SUPPRESSED_WARNINGS)
        list(JOIN MENGINE_SLANG_SUPPRESSED_WARNINGS "," WARNING_LIST)
        set(WARNING_ARGUMENTS -warnings-disable "${WARNING_LIST}")
    endif()

    add_custom_command(
        OUTPUT "${SPIRV_HEADER}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${GENERATED_DIRECTORY}"
        COMMAND "${MENGINE_SLANGC}" "${SHADER_ABSOLUTE}"
            -target spirv
            -entry "${ENTRY_POINT}"
            -profile "${MENGINE_SLANG_PROFILE}"
            ${WARNING_ARGUMENTS}
            -O2
            -source-embed-style u32
            -source-embed-name "${VARIABLE_NAME}"
            -o "${SPIRV_EMBEDDED}"
        COMMAND "${CMAKE_COMMAND}"
            -DIN_FILE=${SPIRV_EMBEDDED}
            -DOUT_FILE=${SPIRV_HEADER}
            -P "${MENGINE_WRAP_SPIRV_SCRIPT}"
        DEPENDS "${SHADER_ABSOLUTE}" "${MENGINE_WRAP_SPIRV_SCRIPT}"
        COMMENT "Compiling ${SHADER_NAME}.slang:${ENTRY_POINT} to embedded SPIR-V"
        VERBATIM
    )

    target_sources(${TARGET_NAME} PRIVATE "${SPIRV_HEADER}")
    target_include_directories(${TARGET_NAME} PRIVATE "${GENERATED_DIRECTORY}")
endfunction()
