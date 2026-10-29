target_sources(
	phoenix_simulation
	PRIVATE
		FILE_SET runtime_partitions
			TYPE CXX_MODULES
			BASE_DIRS "${PROJECT_SOURCE_DIR}"
			FILES "${CMAKE_CURRENT_LIST_DIR}/integration.cppm" "${CMAKE_CURRENT_LIST_DIR}/audio_state.cppm"
	PRIVATE
		"${CMAKE_CURRENT_LIST_DIR}/console_sessions.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/console_views.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/debug_views.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/display_commands.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/knowledge_console_observations.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/scene_views.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/presentation_audio.cpp"
)
