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

if(MSVC)
    target_compile_options(imgui PRIVATE /W0)
else()
    target_compile_options(imgui PRIVATE -w)
endif()
