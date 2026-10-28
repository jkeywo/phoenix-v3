# Shared tool controls/backend are below every authoring and inspection surface.
add_subdirectory(shell)
target_sources(phoenix_workshop_shell PRIVATE ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp)
add_subdirectory(preview)
add_subdirectory(map-editor)
add_subdirectory(gm)
add_subdirectory(layout-editor)
add_subdirectory(tree-editor)
add_subdirectory(test-client)
add_subdirectory(gameplay-editors)
add_subdirectory(workflow)

# Top-level application composition only. Authoring and GM own separate state.
add_library(phoenix_tools STATIC shell/composition.cpp shell/editor_composition.cpp)
target_sources(phoenix_tools PUBLIC FILE_SET CXX_MODULES FILES shell/tools.cppm)
target_link_libraries(
	phoenix_tools
	PUBLIC phoenix_application_shell phoenix_workshop_shell phoenix_workshop_test_client
	PRIVATE phoenix_presentation phoenix_ui_navigation phoenix_workshop_workflow phoenix_workshop_gm
)
if(EMSCRIPTEN)
	target_sources(phoenix_tools PRIVATE test-client/browser_workshop_test.cpp)
endif()

# The root deliberately disables scanning for third-party dependencies. Targets
# created in child directories must opt in for their implementation imports too.
foreach(
	target
	IN
	ITEMS
		phoenix_workshop_shell
		phoenix_workshop_asset_preview
		phoenix_workshop_material_preview
		phoenix_workshop_map_navigation
		phoenix_workshop_map
		phoenix_workshop_gm
		phoenix_workshop_layout
		phoenix_workshop_tree
		phoenix_workshop_test_client
		phoenix_workshop_gameplay
		phoenix_workshop_workflow
		phoenix_tools
)
	set_target_properties(
		${target}
		PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_SCAN_FOR_MODULES ON
	)
endforeach()

target_link_libraries(phoenix_tools PUBLIC phoenix_content_documents)
