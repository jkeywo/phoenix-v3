add_library(phoenix_ui_skin STATIC skin.cpp)
target_sources(phoenix_ui_skin PUBLIC FILE_SET CXX_MODULES FILES skin.cppm)
target_link_libraries(phoenix_ui_skin PUBLIC phoenix_foundation)
add_library(phoenix_ui_theme STATIC theme.cpp)
target_sources(phoenix_ui_theme PUBLIC FILE_SET CXX_MODULES FILES theme.cppm)
target_link_libraries(phoenix_ui_theme PUBLIC phoenix_ui_skin phoenix_ui_rml phoenix_content_catalog raylib)
add_library(phoenix_ui_settings STATIC settings.cpp)
target_sources(phoenix_ui_settings PUBLIC FILE_SET CXX_MODULES FILES settings.cppm)
target_link_libraries(phoenix_ui_settings PUBLIC phoenix_ui_navigation RmlUi::RmlUi)
add_library(phoenix_ui_screens STATIC menu.cpp)
target_sources(phoenix_ui_screens PUBLIC FILE_SET CXX_MODULES FILES screens.cppm)
target_link_libraries(
	phoenix_ui_screens
	PUBLIC phoenix_application_shell RmlUi::RmlUi raylib
	PRIVATE phoenix_ui_theme phoenix_ui_settings phoenix_ui_rml phoenix_qr phoenix_files
)

target_link_libraries(
	phoenix_ui_theme
	PUBLIC phoenix_content_catalog
	PRIVATE phoenix_content_image_codec phoenix_content_ui
)

target_link_libraries(
	phoenix_ui_screens
	PRIVATE
		phoenix_content_assets
		phoenix_content_catalog
		phoenix_content_compilers
		phoenix_content_distribution
		phoenix_content_packs
)
