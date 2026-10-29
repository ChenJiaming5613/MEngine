# Build time check that the C++ mirrors of shader constant structs (render/RenderTypes.hpp) match the Slang layout.
# slangc -reflection-json reports member offsets and sizes under the rules the SPIR-V uses (std140 for ConstantBuffer,
# std430 for push constants); a generated header turns them into static_asserts against offsetof / sizeof.

set(MENGINE_SHADER_LAYOUTS_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/ShaderLayoutsScript.cmake")

# MEngineGenerateShaderLayoutChecks(
#     OUTPUT <header>                  generated header with the static_asserts
#     NAMESPACE <c++ namespace>        namespace of the C++ structs
#     PROBES <source@entry>...         shaders whose parameters use the structs (unused parameters are still reflected)
#     STRUCTS <name>...)               struct names, identical in Slang and C++
function(MEngineGenerateShaderLayoutChecks)
    cmake_parse_arguments(ARG "" "OUTPUT;NAMESPACE" "PROBES;STRUCTS" ${ARGN})

    set(REFLECTION_DIRECTORY "${CMAKE_BINARY_DIR}/shader_reflection")
    file(GLOB COMMON_SHADERS CONFIGURE_DEPENDS "${MENGINE_SHADER_SOURCE_DIR}/common/*.slang")

    set(JSON_FILES)
    foreach(PROBE IN LISTS ARG_PROBES)
        # "<source>@<entry>": ':' would clash with Windows drive letters.
        string(FIND "${PROBE}" "@" SEPARATOR REVERSE)
        if(SEPARATOR LESS 0)
            message(FATAL_ERROR "MEngineGenerateShaderLayoutChecks: probe '${PROBE}' is not <source>@<entry>")
        endif()
        string(SUBSTRING "${PROBE}" 0 ${SEPARATOR} PROBE_SOURCE)
        math(EXPR ENTRY_START "${SEPARATOR} + 1")
        string(SUBSTRING "${PROBE}" ${ENTRY_START} -1 PROBE_ENTRY)
        get_filename_component(PROBE_ABSOLUTE "${PROBE_SOURCE}" ABSOLUTE)
        get_filename_component(PROBE_NAME "${PROBE_SOURCE}" NAME_WE)
        set(JSON_OUTPUT "${REFLECTION_DIRECTORY}/${PROBE_NAME}.${PROBE_ENTRY}.json")
        # Same options as MEngineCompileShader, so the reflected layout is the one of the shipped SPIR-V.
        add_custom_command(
            OUTPUT "${JSON_OUTPUT}"
            COMMAND "${CMAKE_COMMAND}" -E make_directory "${REFLECTION_DIRECTORY}"
            COMMAND "${MENGINE_SLANGC}" "${PROBE_ABSOLUTE}"
                -target spirv
                -entry "${PROBE_ENTRY}"
                -profile "${MENGINE_SLANG_PROFILE}"
                -I "${MENGINE_SHADER_SOURCE_DIR}"
                ${MENGINE_SLANG_WARNING_ARGUMENTS}
                -O2
                -o "${REFLECTION_DIRECTORY}/${PROBE_NAME}.${PROBE_ENTRY}.spv"
                -reflection-json "${JSON_OUTPUT}"
            DEPENDS "${PROBE_ABSOLUTE}" ${COMMON_SHADERS}
            COMMENT "Reflecting ${PROBE_NAME}.slang:${PROBE_ENTRY}"
            VERBATIM
        )
        list(APPEND JSON_FILES "${JSON_OUTPUT}")
    endforeach()

    # Lists travel as "|" separated strings: semicolons do not survive a custom command line on every generator.
    list(JOIN JSON_FILES "|" JSON_FILE_LIST)
    list(JOIN ARG_STRUCTS "|" STRUCT_LIST)
    add_custom_command(
        OUTPUT "${ARG_OUTPUT}"
        COMMAND "${CMAKE_COMMAND}"
            "-DJSON_FILES=${JSON_FILE_LIST}"
            "-DSTRUCTS=${STRUCT_LIST}"
            "-DNAMESPACE=${ARG_NAMESPACE}"
            "-DOUTPUT=${ARG_OUTPUT}"
            -P "${MENGINE_SHADER_LAYOUTS_SCRIPT}"
        DEPENDS ${JSON_FILES} "${MENGINE_SHADER_LAYOUTS_SCRIPT}"
        COMMENT "Generating shader layout checks"
        VERBATIM
    )
endfunction()
