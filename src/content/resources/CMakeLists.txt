add_library(phoenix_content_resources_types STATIC)
target_sources(
	phoenix_content_resources_types
	PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES types.cppm
)
set_target_properties(
	phoenix_content_resources_types
	PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_SCAN_FOR_MODULES ON
)
target_link_libraries(
	phoenix_content_resources_types
	PUBLIC phoenix_foundation phoenix_resources_networks_definitions phoenix_behavior_state_tree_definitions
)

target_sources(
	phoenix_content_compilers
	PRIVATE resource_bindings.cpp stock_catalog.cpp
	PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES compiler.cppm
)
