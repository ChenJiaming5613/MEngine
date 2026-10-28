# tinygltf v3 (C API). tiny_gltf_v3.c also compiles the tinygltf_json_c.h implementation,
# so tg3json_* is available to C++ code by including the header only.
set(MENGINE_TINYGLTF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/tinygltf")

add_library(tinygltf3 STATIC "${MENGINE_TINYGLTF_DIR}/tiny_gltf_v3.c")

set_target_properties(tinygltf3 PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)

target_include_directories(tinygltf3 SYSTEM PUBLIC "${MENGINE_TINYGLTF_DIR}")

# File access goes through MEngine's callbacks (SDL, works with APK assets), so TINYGLTF3_ENABLE_FS stays off.
if(MSVC)
    target_compile_definitions(tinygltf3 PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(tinygltf3 PRIVATE /W0)
else()
    target_compile_options(tinygltf3 PRIVATE -w)
endif()
