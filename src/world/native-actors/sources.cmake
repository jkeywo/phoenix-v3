target_sources(
	phoenix_simulation
	PRIVATE
		FILE_SET runtime_partitions
			TYPE CXX_MODULES
			BASE_DIRS "${PROJECT_SOURCE_DIR}"
			FILES "${CMAKE_CURRENT_LIST_DIR}/integration.cppm"
	PRIVATE "${CMAKE_CURRENT_LIST_DIR}/actor_assembly.cpp" "${CMAKE_CURRENT_LIST_DIR}/equipment_execution.cpp"
)
