# Native window/display composition is separate from portable application data.
add_library(phoenix_native_displays STATIC displays.cpp)
target_sources(phoenix_native_displays PUBLIC FILE_SET CXX_MODULES FILES displays.cppm)
target_link_libraries(
	phoenix_native_displays
	PUBLIC phoenix_application_shell
	PRIVATE phoenix_application_displays phoenix_platform_display_identity raylib
)
set_target_properties(
	phoenix_native_displays
	PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_SCAN_FOR_MODULES ON
)
