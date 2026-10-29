# Khronos SPIRV-Reflect for runtime reflection of the shipped SPIR-V. Only the C implementation is built; it carries its
# own spirv.h.
set(MENGINE_SPIRV_REFLECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/SPIRV-Reflect")

add_library(spirv_reflect STATIC "${MENGINE_SPIRV_REFLECT_DIR}/spirv_reflect.c")

set_target_properties(spirv_reflect PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

target_include_directories(spirv_reflect SYSTEM PUBLIC "${MENGINE_SPIRV_REFLECT_DIR}")

if(MSVC)
    target_compile_definitions(spirv_reflect PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(spirv_reflect PRIVATE /W0)
else()
    target_compile_options(spirv_reflect PRIVATE -w)
endif()
