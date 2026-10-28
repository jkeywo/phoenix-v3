target_sources(
	phoenix_simulation
	PRIVATE
		FILE_SET runtime_partitions
			TYPE CXX_MODULES
			BASE_DIRS "${PROJECT_SOURCE_DIR}"
			FILES "${CMAKE_CURRENT_LIST_DIR}/integration.cppm"
	PRIVATE
		"${CMAKE_CURRENT_LIST_DIR}/away_access.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/away_commands.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/away_projection.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/gameplay_boarding.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/gameplay_compartment_access.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/gameplay_conversations.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/gameplay_ownership.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/gameplay_personnel.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/team_admission.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/team_execution.cpp"
)
