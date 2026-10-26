# Manually invoked diagnostic; deliberately absent from CTest/CI.
if(TARGET phoenix_runtime_profile)
	return()
endif()
add_executable(phoenix_runtime_profile EXCLUDE_FROM_ALL tests/manual/runtime_profile.cpp)
set_property(TARGET phoenix_runtime_profile PROPERTY CXX_SCAN_FOR_MODULES ON)
target_link_libraries(phoenix_runtime_profile PRIVATE phoenix_session phoenix_presentation)
if(NOT EMSCRIPTEN)
	add_executable(phoenix_console_transition_smoke EXCLUDE_FROM_ALL tests/manual/console_transition.cpp)
	target_link_libraries(phoenix_console_transition_smoke PRIVATE phoenix_network_lobby phoenix_presentation)
	add_executable(phoenix_away_assignment_smoke EXCLUDE_FROM_ALL tests/manual/away_assignments.cpp)
	target_link_libraries(phoenix_away_assignment_smoke PRIVATE phoenix_session phoenix_presentation phoenix_core)
	phoenix_fixture(phoenix_away_assignment_smoke)
	add_executable(phoenix_context_docking_smoke EXCLUDE_FROM_ALL tests/manual/context_docking.cpp)
	target_link_libraries(phoenix_context_docking_smoke PRIVATE phoenix_session phoenix_presentation phoenix_core)
	phoenix_fixture(phoenix_context_docking_smoke)
endif()
if(EMSCRIPTEN)
	set(
		PHOENIX_PROFILE_PACK
		"${CMAKE_CURRENT_BINARY_DIR}/base.zip"
		CACHE FILEPATH
		"Pack embedded by the manual performance host"
	)
	set_target_properties(phoenix_runtime_profile PROPERTIES SUFFIX ".html")
	target_link_options(
		phoenix_runtime_profile
		PRIVATE
			"-O3"
			"-sASSERTIONS=1"
			"-sSTACK_OVERFLOW_CHECK=1"
			"--profiling-funcs"
			"-sALLOW_MEMORY_GROWTH=1"
			"-sSTACK_SIZE=1048576"
			"--preload-file"
			"${CMAKE_CURRENT_BINARY_DIR}/assets@/assets"
			"SHELL:--preload-file \"${PHOENIX_PROFILE_PACK}@/assets/base.zip\""
			"--shell-file"
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/manual/runtime_profile.html"
	)
	set_property(
		TARGET phoenix_runtime_profile
		APPEND
		PROPERTY LINK_DEPENDS "${PHOENIX_PROFILE_PACK}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/manual/runtime_profile.html"
	)
else()
	add_dependencies(phoenix_runtime_profile base_pack)
    add_executable(phoenix_effects_fixture EXCLUDE_FROM_ALL tests/manual/effects_fixture.cpp)
    set_property(TARGET phoenix_effects_fixture PROPERTY CXX_SCAN_FOR_MODULES ON)
    target_link_libraries(phoenix_effects_fixture PRIVATE phoenix_content)
    phoenix_fixture(phoenix_effects_fixture)
	# Simulation-only diagnostic: no window, graphics context, pacing or UI work.
	add_executable(phoenix_headless_profile EXCLUDE_FROM_ALL tests/manual/headless_profile.cpp)
	set_property(TARGET phoenix_headless_profile PROPERTY CXX_SCAN_FOR_MODULES ON)
	target_link_libraries(phoenix_headless_profile PRIVATE phoenix_simulation)
	add_dependencies(phoenix_headless_profile base_pack)
endif()
