add_library(phoenix_content_knowledge_types STATIC)
target_sources(
	phoenix_content_knowledge_types
	PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES types.cppm
)
set_target_properties(
	phoenix_content_knowledge_types
	PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_SCAN_FOR_MODULES ON
)
target_link_libraries(
	phoenix_content_knowledge_types
	PUBLIC
		phoenix_content_controllers_types
		phoenix_foundation
		phoenix_knowledge_records_definitions
		phoenix_behavior_state_tree_definitions
)

target_sources(
	phoenix_content_compilers
	PRIVATE gameplay_knowledge.cpp tree_bindings.cpp
	PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES compiler.cppm
)
