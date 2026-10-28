add_library(phoenix_workshop_shell STATIC workshop.cpp preferences.cpp fields.cpp authoring.cpp)
target_sources(phoenix_workshop_shell PUBLIC FILE_SET CXX_MODULES FILES shell.cppm authoring.cppm)
target_link_libraries(
	phoenix_workshop_shell
	PUBLIC phoenix_application_preferences phoenix_imgui raylib
	PRIVATE phoenix_files phoenix_graphics_assets
)

target_link_libraries(
	phoenix_workshop_shell
	PUBLIC phoenix_content_catalog
	PRIVATE phoenix_content_image_codec phoenix_content_ui
)
