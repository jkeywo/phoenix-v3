add_library(phoenix_graphics_audio_policy STATIC audio_policy.cpp)
target_sources(phoenix_graphics_audio_policy PUBLIC FILE_SET CXX_MODULES FILES audio_policy.cppm)
if(EMSCRIPTEN)
	add_library(phoenix_graphics_audio STATIC music.cpp)
else()
	add_library(phoenix_graphics_audio STATIC native_music.cpp native_backend.cpp)
	target_include_directories(phoenix_graphics_audio PRIVATE "${raylib_SOURCE_DIR}/src")
endif()
target_sources(phoenix_graphics_audio PUBLIC FILE_SET CXX_MODULES FILES audio.cppm)
target_link_libraries(phoenix_graphics_audio PUBLIC phoenix_graphics_audio_policy phoenix_graphics_scene raylib)

target_link_libraries(phoenix_graphics_audio_policy PUBLIC phoenix_content_assets)

target_link_libraries(phoenix_graphics_audio PUBLIC phoenix_content_catalog PRIVATE phoenix_content_assets)

if(NOT EMSCRIPTEN)
	add_executable(phoenix_screen_audio_tests "${PROJECT_SOURCE_DIR}/tests/system/screen_audio.cpp")
	target_link_libraries(phoenix_screen_audio_tests PRIVATE phoenix_graphics_audio)
	target_compile_definitions(
		phoenix_screen_audio_tests
		PRIVATE PHOENIX_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/content"
	)
endif()
