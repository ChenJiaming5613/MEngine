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

include(CompileShaders)

set(VMA_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/VulkanMemoryAllocator)

include(ImGui)

set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glm)

add_library(stb INTERFACE)
target_include_directories(stb SYSTEM INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")

set(MENGINE_SOURCES
    source/main.cpp
)

# SDLActivity loads libmain.so.
if(ANDROID)
    add_library(main SHARED ${MENGINE_SOURCES})
    set(MENGINE_TARGET main)
else()
    add_executable(MEngine ${MENGINE_SOURCES})
    set(MENGINE_TARGET MEngine)
endif()

target_link_libraries(${MENGINE_TARGET} PRIVATE
    SDL3::SDL3
    ${MENGINE_VULKAN_LIBRARIES}
    GPUOpen::VulkanMemoryAllocator
    imgui
    glm::glm
    stb
)

if(MSVC)
    target_compile_options(${MENGINE_TARGET} PRIVATE /utf-8 /W4)
else()
    # Designated initializers intentionally leave Vulkan struct members zeroed.
    target_compile_options(${MENGINE_TARGET} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()

# The Android API 24 loader only exports Vulkan 1.0 symbols, so VMA must fetch 1.1 entry points at runtime.
target_compile_definitions(${MENGINE_TARGET} PRIVATE
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    VMA_VULKAN_VERSION=1001000
    VMA_STATIC_VULKAN_FUNCTIONS=0
    VMA_DYNAMIC_VULKAN_FUNCTIONS=1
    STBI_ONLY_PNG
    STBI_ONLY_JPEG
    STBI_NO_STDIO
    $<$<CONFIG:Debug,RelWithDebInfo>:MENGINE_ENABLE_VALIDATION_LAYERS>
)

MEngineEmbedShader(${MENGINE_TARGET} "${CMAKE_CURRENT_SOURCE_DIR}/source/shaders/Unlit.slang" VertexMain g_unlitVertexSpirv)
MEngineEmbedShader(${MENGINE_TARGET} "${CMAKE_CURRENT_SOURCE_DIR}/source/shaders/Unlit.slang" FragmentMain g_unlitFragmentSpirv)

# Android packages assets/ through Gradle (assets.srcDir), so only the desktop build copies them.
if(NOT ANDROID)
    add_custom_command(TARGET ${MENGINE_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:${MENGINE_TARGET}>/assets"
        COMMENT "Copying assets next to the executable"
        VERBATIM
    )
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
