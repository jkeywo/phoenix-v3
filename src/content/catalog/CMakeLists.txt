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
