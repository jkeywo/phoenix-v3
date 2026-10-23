cmake_minimum_required(VERSION 3.24)
project(PhoenixImageCodecs LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
include(FetchContent)
set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(WEBP_USE_THREAD OFF CACHE BOOL "" FORCE)
set(WEBP_ENABLE_SIMD OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
	webp
	URL https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz
	URL_HASH SHA256=e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(webp)
include("${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/WebPScalar.cmake")
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(DEPS "${REPO}/build/native/_deps")
add_library(
	bench_zip
	STATIC
	${DEPS}/miniz-src/miniz.c
	${DEPS}/miniz-src/miniz_tdef.c
	${DEPS}/miniz-src/miniz_tinfl.c
	${DEPS}/miniz-src/miniz_zip.c
)
target_include_directories(bench_zip PUBLIC ${DEPS}/miniz-src ${DEPS}/miniz-build)
add_executable(image_codecs benchmark.cpp)
target_include_directories(image_codecs PRIVATE ${DEPS}/raylib-src/src/external ${DEPS}/json-src/include)
target_link_libraries(image_codecs PRIVATE bench_zip webp)
if(EMSCRIPTEN)
	target_compile_options(image_codecs PRIVATE -fexceptions)
	target_link_options(image_codecs PRIVATE -fexceptions -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576)
endif()
