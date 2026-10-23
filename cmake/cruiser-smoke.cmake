# Explicit authored-content journey; never added to the automatic balance gate.
add_executable(phoenix_cruiser_smoke EXCLUDE_FROM_ALL tests/manual/cruiser.cpp)
set_property(TARGET phoenix_cruiser_smoke PROPERTY CXX_SCAN_FOR_MODULES ON)
target_link_libraries(phoenix_cruiser_smoke PRIVATE phoenix_session phoenix_presentation)
if(EMSCRIPTEN)
	set_target_properties(phoenix_cruiser_smoke PROPERTIES SUFFIX ".html")
	target_link_options(
		phoenix_cruiser_smoke
		PRIVATE
			"-O1"
			"-sALLOW_MEMORY_GROWTH=1"
			"-sSTACK_SIZE=1048576"
			"--preload-file"
			"${CMAKE_CURRENT_BINARY_DIR}/assets@/assets"
			"SHELL:--preload-file \"${CMAKE_CURRENT_BINARY_DIR}/base.zip@/assets/base.zip\""
			"--shell-file"
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/manual/cruiser.html"
	)
	set_property(
		TARGET phoenix_cruiser_smoke
		APPEND
		PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/base.zip" "${CMAKE_CURRENT_SOURCE_DIR}/tests/manual/cruiser.html"
	)
else()
	add_executable(phoenix_cruiser_ui_smoke EXCLUDE_FROM_ALL tests/manual/cruiser_ui.cpp)
	set_property(TARGET phoenix_cruiser_ui_smoke PROPERTY CXX_SCAN_FOR_MODULES ON)
	target_link_libraries(phoenix_cruiser_ui_smoke PRIVATE phoenix_session phoenix_presentation phoenix_core)
	add_dependencies(phoenix_cruiser_ui_smoke base_pack test_missions_pack)
	add_dependencies(phoenix_cruiser_smoke base_pack test_missions_pack)
	add_custom_command(
		TARGET phoenix_cruiser_smoke
		POST_BUILD
		COMMAND
			${CMAKE_COMMAND} -E copy_directory_if_different "${CMAKE_CURRENT_BINARY_DIR}/assets"
			"$<TARGET_FILE_DIR:phoenix_cruiser_smoke>/assets"
	)
endif()
