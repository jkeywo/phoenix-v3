add_library(phoenix_ui_preview STATIC preview.cpp)
target_sources(phoenix_ui_preview PUBLIC FILE_SET CXX_MODULES FILES preview.cppm)
target_link_libraries(
	phoenix_ui_preview
	PUBLIC phoenix_content_catalog raylib
	PRIVATE
		phoenix_ui_console_surface
		phoenix_ui_rml
		phoenix_application_shell
		phoenix_content_compilers
		phoenix_console_controls
)

target_link_libraries(phoenix_ui_preview PUBLIC phoenix_content_catalog phoenix_content_ui)
