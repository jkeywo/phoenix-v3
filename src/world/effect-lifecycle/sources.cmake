target_sources(
	phoenix_simulation
	PRIVATE
		FILE_SET runtime_partitions
			TYPE CXX_MODULES
			BASE_DIRS "${PROJECT_SOURCE_DIR}"
			FILES "${CMAKE_CURRENT_LIST_DIR}/integration.cppm"
	PRIVATE
		"${CMAKE_CURRENT_LIST_DIR}/effects_bindings_adapter.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/effects_damage_rules.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/effects_region_rules.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/effects_rule_interface.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/effects_rule_lifecycle.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/effects_rule_persistence.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/effects_rule_sources.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/effects_rules.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/upkeep_commit.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/upkeep_quotes.cpp"
)
