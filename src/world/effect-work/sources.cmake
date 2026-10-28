target_sources(
	phoenix_simulation
	PRIVATE
		FILE_SET runtime_partitions
			TYPE CXX_MODULES
			BASE_DIRS "${PROJECT_SOURCE_DIR}"
			FILES "${CMAKE_CURRENT_LIST_DIR}/integration.cppm"
	PRIVATE
		"${CMAKE_CURRENT_LIST_DIR}/ability_grants.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/ability_lifecycle.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/effects_ability_interface.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/effects_finite_native_work.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/effects_process_interface.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/effects_process_machinery.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/process_continuation.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/process_execution.cpp"
)
