include(FetchContent)

FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/thedmd/imgui
  GIT_TAG 4981fef6649c1c9204f39641fff7d06cc2b1acfe
  GIT_SHALLOW TRUE
  GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(imgui)

FetchContent_Declare(
  imgui_node_editor
  GIT_REPOSITORY https://github.com/thedmd/imgui-node-editor
  GIT_TAG e78e447900909a051817a760efe13fe83e6e1afc
  GIT_PROGRESS TRUE
)
FetchContent_MakeAvailable(imgui_node_editor)

set(imgui_SOURCE  
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp

	#Add Node-Editor extension
	${imgui_node_editor_SOURCE_DIR}/crude_json.cpp
	${imgui_node_editor_SOURCE_DIR}/imgui_canvas.cpp
	${imgui_node_editor_SOURCE_DIR}/imgui_node_editor.cpp
	${imgui_node_editor_SOURCE_DIR}/imgui_node_editor_api.cpp
)
set(imgui_INCLUDE_FOLDERS
    ${imgui_SOURCE_DIR}
    ${imgui_node_editor_SOURCE_DIR}
)
