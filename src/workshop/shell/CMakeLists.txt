add_library(phoenix_workshop_shell STATIC workshop.cpp preferences.cpp)
target_sources(phoenix_workshop_shell PUBLIC FILE_SET CXX_MODULES FILES shell.cppm)
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

if(PHOENIX_ENABLE_WORKSHOP)
	target_sources(
		phoenix_workshop_shell
		PRIVATE fields.cpp authoring.cpp
		PUBLIC FILE_SET CXX_MODULES FILES authoring.cppm
	)
	target_link_libraries(phoenix_workshop_shell PUBLIC phoenix_workshop_authoring_core)
endif()
