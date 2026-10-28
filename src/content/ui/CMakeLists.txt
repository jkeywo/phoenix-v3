add_library(phoenix_content_ui STATIC ui_layout.cpp widget_bindings.cpp help.cpp)
target_sources(phoenix_content_ui PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES types.cppm)
set_target_properties(
	phoenix_content_ui
	PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_SCAN_FOR_MODULES ON
)
target_link_libraries(phoenix_content_ui PUBLIC phoenix_content_catalog phoenix_content_image_codec phoenix_foundation)
