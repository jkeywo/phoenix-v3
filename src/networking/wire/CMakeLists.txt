add_library(phoenix_wire STATIC packet.cpp transfer.cpp)
target_sources(phoenix_wire PUBLIC FILE_SET CXX_MODULES FILES wire.cppm)
target_link_libraries(
	phoenix_wire
	PUBLIC phoenix_content_catalog phoenix_content_packs phoenix_content_compilers
	PRIVATE phoenix_data
)
target_include_directories(phoenix_wire PRIVATE ${picosha2_SOURCE_DIR})
set_property(TARGET phoenix_wire PROPERTY CXX_SCAN_FOR_MODULES ON)
set_property(
	TARGET phoenix_wire
	PROPERTY
		PHOENIX_ALLOWED_DEPENDENCIES "phoenix_content_catalog;phoenix_content_packs;phoenix_content_compilers;phoenix_data"
)
set_property(GLOBAL APPEND PROPERTY PHOENIX_DEPENDENCY_MODULES phoenix_wire)
