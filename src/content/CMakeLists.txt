# Mutually recursive catalogue compiler implementations share one static target.
# Their public named interfaces depend only on catalogue/definition contracts;
# implementation imports express compiler collaborators without interface cycles.
add_library(phoenix_content_compilers STATIC)
set_target_properties(
	phoenix_content_compilers
	PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_SCAN_FOR_MODULES ON
)
add_subdirectory(assets)
add_subdirectory(catalog)
add_subdirectory(combat)
add_subdirectory(controllers)
add_subdirectory(distribution)
add_subdirectory(documents)
add_subdirectory(effects)
add_subdirectory(equipment)
add_subdirectory(knowledge)
add_subdirectory(logistics)
add_subdirectory(operations)
add_subdirectory(packs)
add_subdirectory(resources)
add_subdirectory(scenarios)
add_subdirectory(sensing)
add_subdirectory(ui)
add_subdirectory(verification)
target_link_libraries(
	phoenix_content_compilers
	PUBLIC
		phoenix_foundation
		phoenix_resources_networks_definitions
		phoenix_knowledge_records_definitions
		phoenix_scenarios_runtime_definitions
		phoenix_behavior_state_tree_definitions
		phoenix_behavior_vocabulary
		phoenix_gameplay_conversations_definitions
		phoenix_gameplay_flight_definitions
		phoenix_content_assets
		phoenix_content_catalog
		phoenix_content_combat_types
		phoenix_content_controllers_types
		phoenix_content_effects_types
		phoenix_content_equipment_types
		phoenix_content_image_codec
		phoenix_content_knowledge_types
		phoenix_content_logistics_types
		phoenix_content_operations_types
		phoenix_content_packs
		phoenix_content_resources_types
		phoenix_content_sensing_types
		phoenix_content_ui
)
