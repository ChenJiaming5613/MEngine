set(MENGINE_IMGUI_NODE_EDITOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui-node-editor")

if(NOT EXISTS "${MENGINE_IMGUI_NODE_EDITOR_DIR}/imgui_node_editor.h")
    message(FATAL_ERROR "imgui-node-editor is missing; run: git submodule update --init third_party/imgui-node-editor")
endif()

add_library(imgui_node_editor STATIC
    "${MENGINE_IMGUI_NODE_EDITOR_DIR}/imgui_node_editor.cpp"
    "${MENGINE_IMGUI_NODE_EDITOR_DIR}/imgui_node_editor_api.cpp"
    "${MENGINE_IMGUI_NODE_EDITOR_DIR}/imgui_canvas.cpp"
    "${MENGINE_IMGUI_NODE_EDITOR_DIR}/crude_json.cpp"
)

target_include_directories(imgui_node_editor PUBLIC "${MENGINE_IMGUI_NODE_EDITOR_DIR}")
target_link_libraries(imgui_node_editor PUBLIC imgui)
target_compile_definitions(imgui_node_editor PRIVATE IMGUI_DEFINE_MATH_OPERATORS)

if(MSVC)
    target_compile_options(imgui_node_editor PRIVATE /W0)
else()
    target_compile_options(imgui_node_editor PRIVATE -w)
endif()
