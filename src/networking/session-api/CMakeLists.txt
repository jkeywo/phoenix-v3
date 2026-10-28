add_library(phoenix_session_api STATIC api.cpp)
target_sources(phoenix_session_api PUBLIC FILE_SET CXX_MODULES FILES api.cppm)
target_link_libraries(
	phoenix_session_api
	PUBLIC phoenix_content_catalog phoenix_content_controllers_types phoenix_transport phoenix_inference
)
set_property(TARGET phoenix_session_api PROPERTY CXX_SCAN_FOR_MODULES ON)
set_property(
	TARGET phoenix_session_api
	PROPERTY
		PHOENIX_ALLOWED_DEPENDENCIES
			"phoenix_content_catalog;phoenix_content_controllers_types;phoenix_transport;phoenix_inference"
)
set_property(GLOBAL APPEND PROPERTY PHOENIX_DEPENDENCY_MODULES phoenix_session_api)
