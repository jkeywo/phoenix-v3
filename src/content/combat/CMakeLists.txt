add_library(phoenix_content_combat_types STATIC serializers.cpp)
target_sources(
	phoenix_content_combat_types
	PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES types.cppm
)
set_target_properties(
	phoenix_content_combat_types
	PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_SCAN_FOR_MODULES ON
)
target_link_libraries(
	phoenix_content_combat_types
	PUBLIC
		phoenix_content_effects_types
		phoenix_content_operations_types
		phoenix_foundation
		phoenix_resources_networks_definitions
)

target_sources(
	phoenix_content_compilers
	PRIVATE combat.cpp directed.cpp
	PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES compiler.cppm
)
