target_sources(
	phoenix_simulation
	PRIVATE
		FILE_SET runtime_partitions
			TYPE CXX_MODULES
			BASE_DIRS "${PROJECT_SOURCE_DIR}"
			FILES "${CMAKE_CURRENT_LIST_DIR}/integration.cppm"
	PRIVATE
		"${CMAKE_CURRENT_LIST_DIR}/craft_admission.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/craft_execution.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/docking_admission.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/docking_execution.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/gameplay_cargo.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/gameplay_ftl.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/gameplay_navigation_assessment.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/supply_admission.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/supply_execution.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/waypoints.cpp"
)
