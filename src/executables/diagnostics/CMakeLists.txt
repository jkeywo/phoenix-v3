if(EMSCRIPTEN)
	add_library(phoenix_browser_frame_profile STATIC browser_profile.cpp)
	target_sources(phoenix_browser_frame_profile PUBLIC FILE_SET CXX_MODULES FILES browser_profile.cppm)
	target_link_libraries(phoenix_browser_frame_profile PUBLIC phoenix_application_shell PRIVATE raylib)
	set_target_properties(
		phoenix_browser_frame_profile
		PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_SCAN_FOR_MODULES ON
	)
else()
	add_library(phoenix_native_frame_profile STATIC native_profile.cpp)
	target_sources(phoenix_native_frame_profile PUBLIC FILE_SET CXX_MODULES FILES native_profile.cppm)
	target_link_libraries(
		phoenix_native_frame_profile
		PUBLIC phoenix_application_shell
		PRIVATE raylib phoenix_session phoenix_content_packs
	)
	set_target_properties(
		phoenix_native_frame_profile
		PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_SCAN_FOR_MODULES ON
	)
endif()
