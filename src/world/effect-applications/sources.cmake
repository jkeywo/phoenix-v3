target_sources(
	phoenix_simulation
	PRIVATE
		FILE_SET runtime_partitions
			TYPE CXX_MODULES
			BASE_DIRS "${PROJECT_SOURCE_DIR}"
			FILES "${CMAKE_CURRENT_LIST_DIR}/integration.cppm"
	PRIVATE
		"${CMAKE_CURRENT_LIST_DIR}/effects_custom_properties.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/effects_effect_application.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/effects_effect_damage.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/effects_native_outcome_effects.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/effects_payload_effects.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/effects_preparation_adapter.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/effects_property_effects.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/effects_store_effects.cpp"
		"${CMAKE_CURRENT_LIST_DIR}/effects_validation_adapter.cpp"
)
