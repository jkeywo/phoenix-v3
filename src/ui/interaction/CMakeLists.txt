add_library(phoenix_ui_interaction STATIC interaction.cpp)
target_sources(phoenix_ui_interaction PUBLIC FILE_SET CXX_MODULES FILES interaction.cppm)
target_link_libraries(phoenix_ui_interaction PUBLIC phoenix_foundation)

add_library(phoenix_ui_navigation STATIC semantics.cpp)
target_sources(phoenix_ui_navigation PUBLIC FILE_SET CXX_MODULES FILES navigation.cppm)
target_link_libraries(
	phoenix_ui_navigation
	PUBLIC
		phoenix_application_shell
		phoenix_ui_interaction
		phoenix_application_preferences
		phoenix_application_preference_store
		phoenix_application_displays
		phoenix_application_connections
)
set_target_properties(
	phoenix_ui_navigation
	PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_SCAN_FOR_MODULES ON
)
