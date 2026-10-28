target_sources(
	phoenix_simulation
	PRIVATE
		FILE_SET runtime_partitions
			TYPE CXX_MODULES
			BASE_DIRS "${PROJECT_SOURCE_DIR}"
			FILES "${CMAKE_CURRENT_LIST_DIR}/integration.cppm"
	PRIVATE
		"${CMAKE_CURRENT_LIST_DIR}/control_bridge.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/control_controller_actions.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/control_controller_coordination.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/control_controller_models.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/control_refresh.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/controller_admission.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/controller_commands.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/controller_runtime.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/equipment_commands.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/world_commands.cpp"
)
