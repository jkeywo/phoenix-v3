add_library(phoenix_content_catalog STATIC model.cpp)
target_sources(
	phoenix_content_catalog
	PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES types.cppm
)
set_target_properties(
	phoenix_content_catalog
	PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_SCAN_FOR_MODULES ON
)
target_link_libraries(
	phoenix_content_catalog
	PUBLIC
		phoenix_content_controllers_types
		phoenix_content_packs
		phoenix_foundation
		phoenix_resources_networks_definitions
		phoenix_knowledge_records_definitions
		phoenix_scenarios_runtime_definitions
		phoenix_behavior_state_tree_definitions
		phoenix_gameplay_conversations_definitions
)

add_library(phoenix_content STATIC)
target_sources(phoenix_content PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES content.cppm)
set_target_properties(
	phoenix_content
	PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_SCAN_FOR_MODULES ON
)
target_link_libraries(
	phoenix_content
	PUBLIC
		phoenix_content_assets
		phoenix_content_catalog
		phoenix_content_combat_types
		phoenix_content_compilers
		phoenix_content_controllers_types
		phoenix_content_distribution
		phoenix_content_documents
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
		phoenix_effects_rules
		phoenix_foundation
		phoenix_resources_networks_definitions
		phoenix_knowledge_records_definitions
		phoenix_scenarios_runtime_definitions
		phoenix_behavior_state_tree_definitions
		phoenix_behavior_vocabulary
		phoenix_gameplay_conversations_definitions
		phoenix_gameplay_flight_definitions
)
