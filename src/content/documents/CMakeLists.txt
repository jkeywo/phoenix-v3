add_library(phoenix_content_documents STATIC workshop.cpp accessors.cpp)
target_sources(
	phoenix_content_documents
	PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES types.cppm
)
set_target_properties(
	phoenix_content_documents
	PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_SCAN_FOR_MODULES ON
)
target_link_libraries(
	phoenix_content_documents
	PUBLIC
		phoenix_content_assets
		phoenix_content_catalog
		phoenix_content_compilers
		phoenix_content_distribution
		phoenix_content_packs
		phoenix_foundation
		phoenix_behavior_state_tree_definitions
		phoenix_knowledge_records_definitions
)
