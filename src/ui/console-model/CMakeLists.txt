add_library(
	phoenix_console_model
	STATIC
	console.cpp
	commands.cpp
	operations.cpp
	presentation.cpp
	formatting.cpp
	availability.cpp
	control_widgets.cpp
)
target_sources(phoenix_console_model PUBLIC FILE_SET CXX_MODULES FILES console.cppm)
target_link_libraries(
	phoenix_console_model
	PUBLIC phoenix_session_api
	PRIVATE phoenix_foundation phoenix_application_preferences
)

target_link_libraries(
	phoenix_console_model
	PUBLIC phoenix_content_controllers_types phoenix_content_ui
	PRIVATE phoenix_content_assets phoenix_content_compilers
)
