target_sources(
	phoenix_content_compilers
	PRIVATE conversations.cpp scenario.cpp
	PUBLIC FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} FILES compiler.cppm
)
