add_library(
	phoenix_workshop_workflow
	STATIC
	editor.cpp
	io.cpp
	appearance.cpp
	consoles.cpp
	diagnostics.cpp
)
target_sources(phoenix_workshop_workflow PUBLIC FILE_SET CXX_MODULES FILES workflow.cppm)
target_link_libraries(
	phoenix_workshop_workflow
	PUBLIC phoenix_application_library phoenix_session_api phoenix_workshop_shell phoenix_workshop_test_client
	PRIVATE
		phoenix_files
		phoenix_workshop_map
		phoenix_workshop_layout
		phoenix_workshop_asset_preview
		phoenix_workshop_material_preview
		phoenix_workshop_gameplay
		phoenix_ui_preview
		phoenix_imgui
		raylib
)

target_link_libraries(
	phoenix_workshop_workflow
	PUBLIC phoenix_content_documents
	PRIVATE phoenix_content_assets phoenix_content_image_codec phoenix_content_packs phoenix_content_ui
)
