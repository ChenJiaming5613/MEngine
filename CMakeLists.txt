cmake_minimum_required(VERSION 3.22)

project(MEngine LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Android 15+ may use 16 KB pages. Must precede add_subdirectory(SDL) so libSDL3.so is aligned as well.
if(ANDROID)
    add_link_options(-Wl,-z,max-page-size=16384)
endif()

set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/SDL)

if(ANDROID)
    set(MENGINE_VULKAN_LIBRARIES vulkan)
else()
    find_package(Vulkan REQUIRED)
    set(MENGINE_VULKAN_LIBRARIES Vulkan::Vulkan)
endif()

include(StageAssets)
include(CompileShaders)

set(VMA_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/VulkanMemoryAllocator)

include(ImGui)
include(TinyGltf)

set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glm)

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")

function(MEngineSetWarnings TARGET_NAME)
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /utf-8 /W4)
    else()
        # Designated initializers intentionally leave Vulkan struct members zeroed.
        target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
    endif()
endfunction()

set(MENGINE_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/source/engine")

# Pure CPU asset code (image decoding, KTX2), shared by the runtime and programs/IblBaker.
file(GLOB_RECURSE MENGINE_ASSET_SOURCES CONFIGURE_DEPENDS "${MENGINE_ENGINE_DIR}/asset/*.cpp" "${MENGINE_ENGINE_DIR}/asset/*.hpp")
add_library(MEngineAsset STATIC ${MENGINE_ASSET_SOURCES})
target_include_directories(MEngineAsset PUBLIC "${MENGINE_ENGINE_DIR}")
target_link_libraries(MEngineAsset PUBLIC glm::glm PRIVATE stb)
target_compile_definitions(MEngineAsset
    PUBLIC
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
    PRIVATE
        STBI_ONLY_PNG
        STBI_ONLY_JPEG
        STBI_ONLY_HDR
        STBI_NO_STDIO
)
MEngineSetWarnings(MEngineAsset)

# Engine: core, platform, rhi, render and the Application base.
file(GLOB_RECURSE MENGINE_CORE_SOURCES CONFIGURE_DEPENDS
    "${MENGINE_ENGINE_DIR}/*.cpp"
    "${MENGINE_ENGINE_DIR}/*.hpp"
)
list(FILTER MENGINE_CORE_SOURCES EXCLUDE REGEX "/engine/asset/")
add_library(MEngineCore STATIC ${MENGINE_CORE_SOURCES})
target_include_directories(MEngineCore PUBLIC "${MENGINE_ENGINE_DIR}")
target_link_libraries(MEngineCore PUBLIC
    MEngineAsset
    SDL3::SDL3
    ${MENGINE_VULKAN_LIBRARIES}
    GPUOpen::VulkanMemoryAllocator
    imgui
    glm::glm
    tinygltf3
)
# The Android API 24 loader only exports Vulkan 1.0 symbols, so VMA must fetch 1.1 entry points at runtime.
target_compile_definitions(MEngineCore PUBLIC
    VMA_VULKAN_VERSION=1001000
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1
    $<$<CONFIG:Debug,RelWithDebInfo>:MENGINE_ENABLE_VALIDATION_LAYERS>
)
MEngineSetWarnings(MEngineCore)

file(GLOB MENGINE_VIEWER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/source/viewer/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/source/viewer/*.hpp"
)

# SDLActivity loads libmain.so.
if(ANDROID)
    add_library(main SHARED ${MENGINE_VIEWER_SOURCES})
    set(MENGINE_TARGET main)
else()
    add_executable(MEngine ${MENGINE_VIEWER_SOURCES})
    set(MENGINE_TARGET MEngine)
endif()

target_link_libraries(${MENGINE_TARGET} PRIVATE MEngineCore)
MEngineSetWarnings(${MENGINE_TARGET})

MEngineCompileShader("${MENGINE_SHADER_SOURCE_DIR}/Pbr.slang" VertexMain)
MEngineCompileShader("${MENGINE_SHADER_SOURCE_DIR}/Pbr.slang" VertexMainTangent)
MEngineCompileShader("${MENGINE_SHADER_SOURCE_DIR}/Pbr.slang" FragmentMain)
MEngineCompileShader("${MENGINE_SHADER_SOURCE_DIR}/Skybox.slang" VertexMain)
MEngineCompileShader("${MENGINE_SHADER_SOURCE_DIR}/Skybox.slang" FragmentMain)
MEngineCompileShader("${MENGINE_SHADER_SOURCE_DIR}/Tonemap.slang" VertexMain)
MEngineCompileShader("${MENGINE_SHADER_SOURCE_DIR}/Tonemap.slang" FragmentMain)
MEngineCompileShader("${MENGINE_SHADER_SOURCE_DIR}/Shadow.slang" VertexMain)
MEngineCompileShader("${MENGINE_SHADER_SOURCE_DIR}/Shadow.slang" VertexMainMask)
MEngineCompileShader("${MENGINE_SHADER_SOURCE_DIR}/Shadow.slang" FragmentMask)
MEngineAddShaderTarget(MEngineShaders)

# Gradle only builds the shared library targets, so staging and shaders hang off the application target.
add_dependencies(MEngineShaders MEngineStageAssets)
add_dependencies(${MENGINE_TARGET} MEngineStageAssets MEngineShaders)

# Android packages the staged directory through Gradle (assets.srcDir); desktop mirrors it next to the executable.
if(NOT ANDROID)
    MEngineMirrorStagedAssets(${MENGINE_TARGET})
endif()

if(WIN32)
    add_custom_command(TARGET ${MENGINE_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:${MENGINE_TARGET}>
            "$<TARGET_FILE_DIR:${MENGINE_TARGET}>"
        COMMENT "Copying runtime DLLs next to the executable"
        COMMAND_EXPAND_LISTS
    )
endif()

if(NOT ANDROID AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/programs/IblBaker/CMakeLists.txt")
    add_subdirectory(programs/IblBaker)
endif()
