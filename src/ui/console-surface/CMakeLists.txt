add_library(
	phoenix_ui_console_surface
	STATIC
	surface.cpp
	interaction.cpp
	layout.cpp
	drawing.cpp
	panels.cpp
	probes.cpp
)
target_sources(phoenix_ui_console_surface PUBLIC FILE_SET CXX_MODULES FILES surface.cppm)
target_link_libraries(
	phoenix_ui_console_surface
	PUBLIC phoenix_application_shell phoenix_console_model phoenix_ui_rml phoenix_ui_interaction phoenix_devices
	PRIVATE
		phoenix_graphics_assets
		phoenix_graphics_radar
		phoenix_graphics_scene
		phoenix_ui_theme
		phoenix_ui_engineering_widgets
		phoenix_ui_information_widgets
		phoenix_ui_operations_widgets
		phoenix_ui_tactical_widgets
		phoenix_ui_flight_widgets
		phoenix_application_preferences
		phoenix_ui_navigation
)

target_link_libraries(
	phoenix_ui_console_surface
	PUBLIC phoenix_content_ui
	PRIVATE phoenix_content_assets phoenix_content_compilers phoenix_content_controllers_types phoenix_content_packs
)
