add_library(phoenix_workshop_test_client STATIC test_client.cpp)
target_sources(phoenix_workshop_test_client PUBLIC FILE_SET CXX_MODULES FILES test_client.cppm)
target_link_libraries(phoenix_workshop_test_client PUBLIC phoenix_workshop_shell PRIVATE phoenix_imgui)

target_link_libraries(phoenix_workshop_test_client PUBLIC phoenix_content_documents)

if(EMSCRIPTEN)
	add_library(phoenix_workshop_browser_test_client STATIC browser_workshop_test.cpp)
	target_sources(phoenix_workshop_browser_test_client PUBLIC FILE_SET CXX_MODULES FILES browser_test_client.cppm)
	target_link_libraries(
		phoenix_workshop_browser_test_client
		PUBLIC phoenix_application_shell
		PRIVATE phoenix_tools phoenix_content_packs
	)
	set_target_properties(
		phoenix_workshop_browser_test_client
		PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_SCAN_FOR_MODULES ON
	)
endif()
