add_library(phoenix_ui_viewscreen STATIC game.cpp geometry.cpp)
target_sources(phoenix_ui_viewscreen PUBLIC FILE_SET CXX_MODULES FILES viewscreen.cppm)
target_link_libraries(
	phoenix_ui_viewscreen
	PUBLIC phoenix_application_shell RmlUi::RmlUi raylib phoenix_graphics_scene
	PRIVATE
		phoenix_console_model
		phoenix_graphics_geometry
		phoenix_graphics_assets
		phoenix_graphics_radar
		phoenix_application_preferences
		phoenix_knowledge_read_models
		phoenix_files
		phoenix_qr
)

target_link_libraries(
	phoenix_ui_viewscreen
	PRIVATE phoenix_content_assets phoenix_content_catalog phoenix_content_compilers phoenix_content_image_codec
)
