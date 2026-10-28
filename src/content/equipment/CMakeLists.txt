add_library(phoenix_content_equipment_types STATIC type_methods.cpp)
target_sources(
	phoenix_content_equipment_types
	PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES types.cppm
)
set_target_properties(
	phoenix_content_equipment_types
	PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_SCAN_FOR_MODULES ON
)
target_link_libraries(phoenix_content_equipment_types PUBLIC phoenix_foundation)

target_sources(
	phoenix_content_compilers
	PRIVATE equipment.cpp factions.cpp gameplay.cpp
	PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES compiler.cppm
)
