add_library(phoenix_content_distribution STATIC console_assets.cpp metadata.cpp)
target_sources(
	phoenix_content_distribution
	PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES compiler.cppm
)
set_target_properties(
	phoenix_content_distribution
	PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_SCAN_FOR_MODULES ON
)
target_link_libraries(
	phoenix_content_distribution
	PUBLIC
		phoenix_content_assets
		phoenix_content_catalog
		phoenix_content_compilers
		phoenix_content_image_codec
		phoenix_content_packs
		phoenix_content_ui
		phoenix_foundation
)
