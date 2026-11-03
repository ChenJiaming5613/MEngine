set(MENGINE_IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui")

add_library(imgui STATIC
    "${MENGINE_IMGUI_DIR}/imgui.cpp"
    "${MENGINE_IMGUI_DIR}/imgui_draw.cpp"
    "${MENGINE_IMGUI_DIR}/imgui_tables.cpp"
    "${MENGINE_IMGUI_DIR}/imgui_widgets.cpp"
    "${MENGINE_IMGUI_DIR}/imgui_demo.cpp"
    "${MENGINE_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp"
    "${MENGINE_IMGUI_DIR}/backends/imgui_impl_vulkan.cpp"
)

target_include_directories(imgui PUBLIC
    "${MENGINE_IMGUI_DIR}"
    "${MENGINE_IMGUI_DIR}/backends"
)

target_link_libraries(imgui PUBLIC SDL3::SDL3 ${MENGINE_VULKAN_LIBRARIES})

# Never force IMGUI_IMPL_VULKAN_HAS_DYNAMIC_RENDERING off: the backend defines it and it changes struct layout. Only
# UseDynamicRendering = false at runtime matters.
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

# Route the Vulkan backend through volk's global dispatch table instead of statically linked function prototypes.
# Defining this makes imgui_impl_vulkan.h include <volk.h>; with VOLK_H_ defined, imgui skips its own loader path and
# calls vk* through the same volk pointers GfxDevice initializes. Without it, imgui references vk* as functions while
# volk.c defines them as data, which the linker silently binds to volk's storage and crashes at ImGui_ImplVulkan_Init.
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK)
target_include_directories(imgui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/volk")

if(MSVC)
    target_compile_options(imgui PRIVATE /W0)
else()
    target_compile_options(imgui PRIVATE -w)
endif()
