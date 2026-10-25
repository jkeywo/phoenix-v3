# Manually invoked diagnostic; deliberately absent from CTest/CI.
if(TARGET phoenix_runtime_profile)
	return()
endif()
add_executable(phoenix_runtime_profile EXCLUDE_FROM_ALL tests/manual/runtime_profile.cpp)
set_property(TARGET phoenix_runtime_profile PROPERTY CXX_SCAN_FOR_MODULES ON)
target_link_libraries(phoenix_runtime_profile PRIVATE phoenix_session phoenix_presentation)
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
	# Simulation-only diagnostic: no window, graphics context, pacing or UI work.
	add_executable(phoenix_headless_profile EXCLUDE_FROM_ALL tests/manual/headless_profile.cpp)
	set_property(TARGET phoenix_headless_profile PROPERTY CXX_SCAN_FOR_MODULES ON)
	target_link_libraries(phoenix_headless_profile PRIVATE phoenix_simulation)
	add_dependencies(phoenix_headless_profile base_pack)
endif()
