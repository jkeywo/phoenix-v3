add_library(phoenix_content_image_codec STATIC image_codec.cpp)
target_sources(
	phoenix_content_image_codec
	PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES image_codec.cppm
)
set_target_properties(
	phoenix_content_image_codec
	PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_SCAN_FOR_MODULES ON
)
target_link_libraries(phoenix_content_image_codec PUBLIC phoenix_foundation)
target_link_libraries(phoenix_content_image_codec PRIVATE webp)
target_include_directories(phoenix_content_image_codec PRIVATE ${raylib_SOURCE_DIR}/src/external)

add_library(phoenix_content_assets STATIC sprite.cpp model.cpp audio.cpp)
target_sources(
	phoenix_content_assets
	PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES types.cppm
)
set_target_properties(
	phoenix_content_assets
	PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_SCAN_FOR_MODULES ON
)
target_link_libraries(
	phoenix_content_assets
	PUBLIC phoenix_content_catalog phoenix_content_image_codec phoenix_foundation
)
target_include_directories(phoenix_content_assets PRIVATE ${raylib_SOURCE_DIR}/src/external)
