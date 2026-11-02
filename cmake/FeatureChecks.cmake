# Inspect the final graph, including links registered after this file is read.
function(phoenix_check_feature_dependencies)
	foreach(runtime native server client)
		if(NOT TARGET ${runtime})
			continue()
		endif()
		if(NOT PHOENIX_ENABLE_WORKSHOP)
			phoenix_forbid_dependencies(
				${runtime}
				"phoenix_workshop_workflow;phoenix_workshop_authoring_core;phoenix_workshop_test_host;phoenix_native_workshop_bridge;phoenix_workshop_browser_test_client"
			)
		endif()
		if(NOT PHOENIX_ENABLE_GM)
			phoenix_forbid_dependencies(${runtime} "phoenix_workshop_gm")
		endif()
		if(NOT PHOENIX_ENABLE_GM AND NOT PHOENIX_ENABLE_WORKSHOP)
			phoenix_forbid_dependencies(${runtime} "phoenix_tools;phoenix_workshop_shell;phoenix_imgui")
		endif()
	endforeach()
	if(TARGET phoenix_base_verifier)
		phoenix_forbid_dependencies(
			phoenix_base_verifier
			"phoenix_simulation;phoenix_tools;phoenix_workshop_test_host;phoenix_imgui;raylib;flecs_static"
		)
	endif()
endfunction()
cmake_language(DEFER DIRECTORY "${PROJECT_SOURCE_DIR}" CALL phoenix_check_feature_dependencies)
