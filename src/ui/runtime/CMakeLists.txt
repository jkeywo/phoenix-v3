add_library(phoenix_ui_rml STATIC rml.cpp)
target_sources(phoenix_ui_rml PUBLIC FILE_SET CXX_MODULES FILES rml.cppm)
target_link_libraries(phoenix_ui_rml PUBLIC phoenix_foundation RmlUi::RmlUi raylib PRIVATE phoenix_shaping)
add_library(phoenix_ui_pad_dispatch STATIC pad_dispatch.cpp)
target_sources(phoenix_ui_pad_dispatch PUBLIC FILE_SET CXX_MODULES FILES pad_dispatch.cppm)
target_link_libraries(
	phoenix_ui_pad_dispatch
	PUBLIC phoenix_ui_console_surface phoenix_ui_interaction
	PRIVATE phoenix_ui_navigation phoenix_graphics_radar phoenix_accessibility RmlUi::RmlUi raylib
)
