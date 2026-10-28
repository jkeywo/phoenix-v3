target_sources(
	phoenix_simulation
	PRIVATE
		FILE_SET runtime_partitions
			TYPE CXX_MODULES
			BASE_DIRS "${PROJECT_SOURCE_DIR}"
			FILES "${CMAKE_CURRENT_LIST_DIR}/integration.cppm"
	PRIVATE
		"${CMAKE_CURRENT_LIST_DIR}/combat_continuation.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/combat_execution.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/gameplay_combat_effects.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/gameplay_combat_ordnance.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/gameplay_combat_weapons.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/gameplay_directed.cpp"
)
