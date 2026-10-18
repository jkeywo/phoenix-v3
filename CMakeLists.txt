cmake_minimum_required(VERSION 3.28)
project(PhoenixV3 VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static dependencies" FORCE)
include(FetchContent)

function(phoenix_fixture target)
	if(EMSCRIPTEN)
		target_compile_definitions(${target} PRIVATE PHOENIX_FIXTURE_DIR="/fixtures/content")
		target_link_options(
			${target}
			PRIVATE
				"-sALLOW_MEMORY_GROWTH=1"
				"-sSTACK_SIZE=1048576"
				"-sSTACK_OVERFLOW_CHECK=2"
				"--preload-file"
				"${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/content@/fixtures/content"
		)
	else()
		target_compile_definitions(${target} PRIVATE PHOENIX_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/content")
	endif()
endfunction()

set(FETCHCONTENT_QUIET OFF)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FLECS_SHARED OFF CACHE BOOL "" FORCE)
set(FLECS_STATIC ON CACHE BOOL "" FORCE)
FetchContent_Declare(
	flecs
	URL https://codeload.github.com/SanderMertens/flecs/tar.gz/d7d0c4f7afb4518a6bae749efdc52c7cb5cffee6
	URL_HASH SHA256=F0030372BCB82F5601D08A489945D55409074C4430B9D2C2983C82492E3F8E85
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
	raylib
	URL https://codeload.github.com/raysan5/raylib/tar.gz/dbc56a87da87d973a9c5baa4e7438a9d20121d28
	URL_HASH SHA256=81B06CE7C19CF3B634B0271C23C361BA6AD8BF45FB8B036ABBFEB4260EC1E126
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
FetchContent_Declare(
	freetype
	URL https://codeload.github.com/freetype/freetype/tar.gz/534ad3456055ee1f65ecde3bcf22a656a31514d1
	URL_HASH SHA256=63FCF92CB44EFA3B169207BFF068676CD8EA188CCECC893812B1877AD36855C5
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
if(EMSCRIPTEN)
	# Sprite surface depth and shadow shaders require WebGL 2 / GLSL ES 3.
	set(OPENGL_VERSION "ES 3.0" CACHE STRING "Phoenix browser graphics baseline" FORCE)
endif()
FetchContent_MakeAvailable(flecs raylib freetype)
add_library(Freetype::Freetype ALIAS freetype)
set(FREETYPE_INCLUDE_DIRS "${freetype_SOURCE_DIR}/include" CACHE STRING "" FORCE)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
	rmlui
	URL https://codeload.github.com/mikke89/RmlUi/tar.gz/refs/tags/6.2
	URL_HASH SHA256=814C3FF7B9666280338D8F0DDA85979F5DAF028D01C85FC8975431D1E2FD8E8B
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
	imgui
	URL https://codeload.github.com/ocornut/imgui/tar.gz/8a223b61bd671687b254458333bb1e3fad50ed5f
	URL_HASH SHA256=AF1EFF67CB527AA80B5F9B48249E291C5AFF2E9E69C39B2F30276CD1F7B9A200
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(rmlui imgui)
set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
set(HB_HAVE_FREETYPE ON CACHE BOOL "" FORCE)
set(HB_HAVE_GLIB OFF CACHE BOOL "" FORCE)
set(HB_HAVE_ICU OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
	harfbuzz
	URL https://codeload.github.com/harfbuzz/harfbuzz/tar.gz/refs/tags/11.2.1
	URL_HASH SHA256=057D5754C3AC0C499BBF4D729D52ACF134C7BB4BA8868BA22E84AE96BC272816
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(harfbuzz)
# Use the font engine shipped with the exact pinned RmlUi revision.
file(GLOB phoenix_shaping_sources CONFIGURE_DEPENDS "${rmlui_SOURCE_DIR}/Samples/basic/harfbuzz/src/*.cpp")
list(FILTER phoenix_shaping_sources EXCLUDE REGEX "/main.cpp$")
add_library(phoenix_shaping STATIC ${phoenix_shaping_sources})
target_include_directories(phoenix_shaping PUBLIC "${rmlui_SOURCE_DIR}/Samples/basic/harfbuzz/src")
target_include_directories(phoenix_shaping PRIVATE "${rmlui_SOURCE_DIR}/Source/Core")
target_link_libraries(phoenix_shaping PUBLIC RmlUi::RmlUi Freetype::Freetype harfbuzz)
if(NOT EMSCRIPTEN)
	set(SDL_SHARED OFF CACHE BOOL "" FORCE)
	set(SDL_STATIC ON CACHE BOOL "" FORCE)
	set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
	set(SDL_TESTS OFF CACHE BOOL "" FORCE)
	set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(SDL_VIDEO OFF CACHE BOOL "" FORCE)
	set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
	set(SDL_RENDER OFF CACHE BOOL "" FORCE)
	set(SDL_GPU OFF CACHE BOOL "" FORCE)
	set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
	set(SDL_HIDAPI ON CACHE BOOL "" FORCE)
	set(SDL_JOYSTICK ON CACHE BOOL "" FORCE)
	set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
	set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
	FetchContent_Declare(
		sdl
		URL https://codeload.github.com/libsdl-org/SDL/tar.gz/refs/tags/release-3.4.16
		URL_HASH SHA256=C2EE715E42EC520C4D11FD8D249EF1D2B2BAF4AD31148B72B3B000276C0B3633
		DOWNLOAD_EXTRACT_TIMESTAMP TRUE
	)
	FetchContent_MakeAvailable(sdl)
endif()
FetchContent_Declare(
	json
	URL https://codeload.github.com/nlohmann/json/tar.gz/9cca280a4d0ccf0c08f47a99aa71d1b0e52f8d03
	URL_HASH SHA256=0DBC5E40A01FF142E7E68C03E85247A4DCEDE2F592D12D3677DEE3664D17975A
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
	miniz
	URL https://codeload.github.com/richgel999/miniz/tar.gz/293d4db1b7d0ffee9756d035b9ac6f7431ef8492
	URL_HASH SHA256=27D99216E9127D1678085639F3059F6907A02C044E8A38392A39F96163DDBBF0
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
	SOURCE_SUBDIR phoenix-no-cmake
)
FetchContent_Declare(
	picosha2
	URL https://codeload.github.com/okdshin/PicoSHA2/tar.gz/161cb3fc4170fa7a3eca9e582cebd27cc4d1fe29
	URL_HASH SHA256=6CF473A00C98298D3DDEE0AED853E3C799791F49DBC01C996EA46CB248E85802
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
	SOURCE_SUBDIR phoenix-no-cmake
)
FetchContent_MakeAvailable(json miniz picosha2)
FetchContent_Declare(
	qrcodegen
	URL https://codeload.github.com/nayuki/QR-Code-generator/tar.gz/3c6d0b3cefb4e049dc337e82237c9644399716a8
	URL_HASH SHA256=EA6326674B89E3AF6D055DAC722D5D03FFD3A1D3EDB5ACD8D691A8B89236907B
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
	SOURCE_SUBDIR phoenix-no-cmake
)
FetchContent_MakeAvailable(qrcodegen)
add_library(phoenix_qr STATIC ${qrcodegen_SOURCE_DIR}/cpp/qrcodegen.cpp)
target_include_directories(phoenix_qr PUBLIC ${qrcodegen_SOURCE_DIR}/cpp)
if(EMSCRIPTEN)
	target_compile_options(phoenix_qr PRIVATE -fexceptions)
endif()
file(CONFIGURE OUTPUT "${miniz_BINARY_DIR}/miniz_export.h" CONTENT "#pragma once\n#define MINIZ_EXPORT\n")
add_library(
	phoenix_zip
	STATIC
	${miniz_SOURCE_DIR}/miniz.c
	${miniz_SOURCE_DIR}/miniz_tdef.c
	${miniz_SOURCE_DIR}/miniz_tinfl.c
	${miniz_SOURCE_DIR}/miniz_zip.c
)
target_include_directories(phoenix_zip PUBLIC ${miniz_SOURCE_DIR} ${miniz_BINARY_DIR})
add_library(
	phoenix_imgui
	STATIC
	${imgui_SOURCE_DIR}/imgui.cpp
	${imgui_SOURCE_DIR}/imgui_draw.cpp
	${imgui_SOURCE_DIR}/imgui_tables.cpp
	${imgui_SOURCE_DIR}/imgui_widgets.cpp
)
target_include_directories(phoenix_imgui PUBLIC ${imgui_SOURCE_DIR})
add_library(phoenix_foundation STATIC src/core/input.cpp src/core/data.cpp src/platform/random.cpp)
target_include_directories(phoenix_foundation PRIVATE ${picosha2_SOURCE_DIR})
if(NOT EMSCRIPTEN)
	target_link_libraries(phoenix_foundation PRIVATE bcrypt)
endif()
target_sources(
	phoenix_foundation
	PUBLIC FILE_SET CXX_MODULES FILES src/core/input.cppm src/core/contracts.cppm src/core/data.cppm
)
target_link_libraries(phoenix_foundation PUBLIC nlohmann_json::nlohmann_json)
if(EMSCRIPTEN)
	target_compile_options(phoenix_foundation PUBLIC -fexceptions)
	target_link_options(phoenix_foundation INTERFACE -fexceptions)
endif()
add_library(phoenix_session_api STATIC)
target_link_libraries(phoenix_session_api PUBLIC phoenix_content phoenix_transport)
add_library(phoenix_core STATIC src/core/application.cpp src/core/semantics.cpp src/core/console.cpp src/core/menu.cpp)
target_link_libraries(phoenix_core PUBLIC phoenix_foundation phoenix_session_api)
add_library(phoenix_replication STATIC src/replication/consensus.cpp)
target_link_libraries(phoenix_replication PUBLIC phoenix_foundation nlohmann_json::nlohmann_json)
add_library(
	phoenix_content
	STATIC
	src/content/pack.cpp
	src/content/catalog.cpp
	src/content/sprite.cpp
	src/content/stations.cpp
	src/content/console_assets.cpp
	src/content/workshop.cpp
	src/behavior/tree.cpp
)
target_link_libraries(phoenix_content PUBLIC phoenix_foundation nlohmann_json::nlohmann_json PRIVATE phoenix_zip)
add_library(phoenix_persistence STATIC src/replication/store.cpp src/replication/durable.cpp)
target_link_libraries(phoenix_persistence PUBLIC phoenix_replication)
if(EMSCRIPTEN)
	target_sources(phoenix_persistence PRIVATE src/platform/browser_store.cpp)
	add_executable(phoenix_storage_probe tests/manual/browser_store.cpp)
	target_link_libraries(phoenix_storage_probe PRIVATE phoenix_persistence)
	target_link_options(
		phoenix_storage_probe
		PRIVATE
			"-sALLOW_MEMORY_GROWTH=1"
			"-sSTACK_SIZE=1048576"
			"-sEXPORTED_FUNCTIONS=['_main','_malloc','_free']"
			"-sEXPORTED_RUNTIME_METHODS=['ccall']"
	)
	configure_file(web/storage-probe.html "${CMAKE_CURRENT_BINARY_DIR}/storage-probe.html" COPYONLY)
else()
	target_sources(phoenix_persistence PRIVATE src/platform/windows_store.cpp)
endif()
target_include_directories(phoenix_content PRIVATE ${raylib_SOURCE_DIR}/src/external)
add_library(phoenix_wire STATIC src/network/packet.cpp src/network/transfer.cpp)
target_link_libraries(phoenix_wire PUBLIC phoenix_content)
target_include_directories(phoenix_wire PRIVATE ${picosha2_SOURCE_DIR})
add_library(phoenix_blobs STATIC src/network/blob.cpp)
target_link_libraries(phoenix_blobs PUBLIC phoenix_foundation)
if(EMSCRIPTEN)
	target_sources(phoenix_blobs PRIVATE src/platform/browser_blobs.cpp)
else()
	target_sources(phoenix_blobs PRIVATE src/platform/windows_blobs.cpp)
endif()
add_library(phoenix_simulation STATIC src/simulation/world.cpp src/simulation/collision.cpp)
target_link_libraries(phoenix_simulation PUBLIC phoenix_foundation phoenix_content PRIVATE flecs_static)
add_library(phoenix_session STATIC src/session/local.cpp src/session/execution.cpp)
target_link_libraries(phoenix_session PUBLIC phoenix_simulation phoenix_session_api)
add_library(phoenix_files STATIC src/platform/imports.cpp)
target_link_libraries(phoenix_files PUBLIC phoenix_content)
add_library(
	phoenix_presentation
	STATIC
	src/presentation/screen.cpp
	src/presentation/game.cpp
	src/presentation/menu.cpp
	src/presentation/music.cpp
	src/presentation/scene.cpp
	src/presentation/icons.cpp
)
add_library(phoenix_devices STATIC)
if(EMSCRIPTEN)
	target_sources(phoenix_devices PRIVATE src/platform/browser_devices.cpp)
	target_compile_options(phoenix_devices PUBLIC -fexceptions)
else()
	target_sources(phoenix_devices PRIVATE src/platform/windows_devices.cpp)
	target_link_libraries(phoenix_devices PRIVATE SDL3::SDL3-static comctl32)
endif()
if(EMSCRIPTEN)
	target_sources(phoenix_presentation PRIVATE src/presentation/browser_semantics.cpp)
else()
	enable_testing()
	add_executable(phoenix_menu_tests tests/system/menu.cpp)
	target_link_libraries(phoenix_menu_tests PRIVATE phoenix_core phoenix_session)
	phoenix_fixture(phoenix_menu_tests)
	add_test(NAME system.native-menu-library COMMAND phoenix_menu_tests)
	add_executable(phoenix_system_tests tests/system/input_and_semantics.cpp)
	target_link_libraries(phoenix_system_tests PRIVATE phoenix_core)
	add_test(NAME system.input-and-semantics COMMAND phoenix_system_tests)
	add_executable(phoenix_content_tests tests/system/content.cpp)
	target_link_libraries(phoenix_content_tests PRIVATE phoenix_content)
	phoenix_fixture(phoenix_content_tests)
	add_test(NAME system.content-and-packs COMMAND phoenix_content_tests)
	add_executable(phoenix_simulation_tests tests/system/simulation.cpp)
	target_link_libraries(phoenix_simulation_tests PRIVATE phoenix_simulation)
	phoenix_fixture(phoenix_simulation_tests)
	add_test(NAME system.motion-radar-trees COMMAND phoenix_simulation_tests)
	add_executable(phoenix_session_tests tests/system/session.cpp)
	target_link_libraries(phoenix_session_tests PRIVATE phoenix_session phoenix_core)
	phoenix_fixture(phoenix_session_tests)
	add_test(NAME system.local-session COMMAND phoenix_session_tests)
	add_executable(phoenix_file_tests tests/system/file_imports.cpp)
	target_link_libraries(phoenix_file_tests PRIVATE phoenix_files phoenix_session)
	phoenix_fixture(phoenix_file_tests)
	add_test(NAME system.file-imports COMMAND phoenix_file_tests)
	add_executable(phoenix_replication_tests tests/system/replication.cpp)
	target_link_libraries(phoenix_replication_tests PRIVATE phoenix_replication phoenix_simulation)
	phoenix_fixture(phoenix_replication_tests)
	add_test(NAME system.peer-consensus COMMAND phoenix_replication_tests)
	add_executable(phoenix_store_tests tests/system/peer_store.cpp)
	target_link_libraries(phoenix_store_tests PRIVATE phoenix_persistence phoenix_content)
	add_test(NAME system.peer-storage COMMAND phoenix_store_tests)
	add_executable(phoenix_transfer_tests tests/system/network_content.cpp)
	target_link_libraries(phoenix_transfer_tests PRIVATE phoenix_wire)
	phoenix_fixture(phoenix_transfer_tests)
	add_test(NAME system.network-content COMMAND phoenix_transfer_tests)
	add_executable(phoenix_blob_tests tests/system/blob_store.cpp)
	target_link_libraries(phoenix_blob_tests PRIVATE phoenix_blobs phoenix_wire)
	phoenix_fixture(phoenix_blob_tests)
	add_test(NAME system.persistent-content COMMAND phoenix_blob_tests)
	add_executable(phoenix_verify src/entry/verify.cpp)
	target_link_libraries(phoenix_verify PRIVATE phoenix_content)
	add_executable(phoenix_pack src/entry/pack.cpp)
	target_link_libraries(phoenix_pack PRIVATE phoenix_content)
	# Track archive contents and membership, including removed files. The list file
	# changes only when membership changes, so reconfiguration alone does not repack.
	file(
		GLOB_RECURSE phoenix_training_sources
		LIST_DIRECTORIES FALSE
		CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/content/training/*"
	)
	file(
		CONFIGURE
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/training-pack-inputs.txt"
		CONTENT "${phoenix_training_sources}"
		@ONLY
	)
	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/training.zip"
		COMMAND phoenix_pack "${CMAKE_CURRENT_SOURCE_DIR}/content/training" "${CMAKE_CURRENT_BINARY_DIR}/training.zip"
		DEPENDS phoenix_pack ${phoenix_training_sources} "${CMAKE_CURRENT_BINARY_DIR}/training-pack-inputs.txt"
		VERBATIM
	)
	add_custom_target(training_pack ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/training.zip")
	target_sources(phoenix_presentation PRIVATE src/presentation/windows_semantics.cpp)
	target_link_libraries(
		phoenix_presentation
		PRIVATE uiautomationcore ole32 oleaut32 comctl32 imm32
	)
endif()

target_link_libraries(
	phoenix_presentation
	PUBLIC phoenix_core phoenix_content phoenix_files raylib RmlUi::RmlUi
	PRIVATE phoenix_qr phoenix_devices phoenix_shaping
)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/assets")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/server-audio")
foreach(audio_file IN ITEMS menu.ogg ambient.ogg CREDITS.txt)
	configure_file("content/audio/${audio_file}" "${CMAKE_CURRENT_BINARY_DIR}/server-audio/${audio_file}" COPYONLY)
endforeach()
configure_file("content/locales.json" "${CMAKE_CURRENT_BINARY_DIR}/assets/locales.json" COPYONLY)
foreach(font Cairo-Regular.ttf Poppins-Regular.ttf LICENSE.txt)
	configure_file(
		"${rmlui_SOURCE_DIR}/Samples/basic/harfbuzz/data/${font}"
		"${CMAKE_CURRENT_BINARY_DIR}/assets/shaping-${font}"
		COPYONLY
	)
endforeach()
configure_file("${harfbuzz_SOURCE_DIR}/COPYING" "${CMAKE_CURRENT_BINARY_DIR}/assets/HARFBUZZ-LICENSE.txt" COPYONLY)
configure_file(
	"${rmlui_SOURCE_DIR}/Samples/assets/LatoLatin-Regular.ttf"
	"${CMAKE_CURRENT_BINARY_DIR}/assets/LatoLatin-Regular.ttf"
	COPYONLY
)
configure_file(
	"${rmlui_SOURCE_DIR}/Samples/assets/LICENSE.txt"
	"${CMAKE_CURRENT_BINARY_DIR}/assets/FONT-LICENSE.txt"
	COPYONLY
)
configure_file("${qrcodegen_SOURCE_DIR}/Readme.markdown" "${CMAKE_CURRENT_BINARY_DIR}/assets/QR-LICENSE.txt" COPYONLY)
add_library(
	phoenix_tools
	STATIC
	src/tools/workshop.cpp
	src/tools/editor.cpp
	${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)
target_link_libraries(phoenix_tools PUBLIC phoenix_presentation phoenix_imgui phoenix_content)
if(EMSCRIPTEN)
	target_sources(phoenix_files PRIVATE src/platform/browser_files.cpp)
else()
	target_sources(phoenix_files PRIVATE src/platform/windows_files.cpp)
	target_link_libraries(phoenix_files PRIVATE comdlg32)
endif()
if(EMSCRIPTEN)
	add_executable(phoenix_blob_probe tests/manual/browser_blobs.cpp)
	target_link_libraries(phoenix_blob_probe PRIVATE phoenix_blobs phoenix_wire)
	phoenix_fixture(phoenix_blob_probe)
	target_link_options(
		phoenix_blob_probe
		PRIVATE "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free']" "-sEXPORTED_RUNTIME_METHODS=['ccall']"
	)
	configure_file(web/blob-probe.html "${CMAKE_CURRENT_BINARY_DIR}/blob-probe.html" COPYONLY)
	add_executable(phoenix_build_check src/entry/build_check.cpp)
	target_link_libraries(phoenix_build_check PRIVATE phoenix_simulation)
	target_link_options(
		phoenix_build_check
		PRIVATE "-sALLOW_MEMORY_GROWTH=1" "-sASSERTIONS=2" "-sSTACK_SIZE=1048576" "-sSTACK_OVERFLOW_CHECK=2" "-g2"
	)
	add_executable(phoenix_content_check tests/system/content.cpp)
	target_link_libraries(phoenix_content_check PRIVATE phoenix_content)
	phoenix_fixture(phoenix_content_check)
	add_executable(phoenix_transfer_check tests/system/network_content.cpp)
	target_link_libraries(phoenix_transfer_check PRIVATE phoenix_wire)
	phoenix_fixture(phoenix_transfer_check)
	add_executable(phoenix_replication_check tests/system/replication.cpp)
	target_link_libraries(phoenix_replication_check PRIVATE phoenix_replication phoenix_simulation)
	phoenix_fixture(phoenix_replication_check)
	add_executable(phoenix_session_check tests/system/session.cpp)
	target_link_libraries(phoenix_session_check PRIVATE phoenix_session phoenix_core)
	phoenix_fixture(phoenix_session_check)
	add_executable(phoenix_file_check tests/system/file_imports.cpp)
	target_link_libraries(phoenix_file_check PRIVATE phoenix_files phoenix_session)
	phoenix_fixture(phoenix_file_check)
	add_executable(phoenix_simulation_check tests/system/simulation.cpp)
	target_link_libraries(phoenix_simulation_check PRIVATE phoenix_simulation)
	phoenix_fixture(phoenix_simulation_check)
	foreach(role IN ITEMS server client workshop)
		add_executable(${role} src/entry/web.cpp)
		target_compile_definitions(${role} PRIVATE PHOENIX_ROLE="${role}")
		target_link_libraries(${role} PRIVATE phoenix_presentation)
		target_link_options(
			${role}
			PRIVATE
				"-sALLOW_MEMORY_GROWTH=1"
				"-sASSERTIONS=1"
				"-sFORCE_FILESYSTEM=1"
				"-sSTACK_SIZE=1048576"
				"-sSTACK_OVERFLOW_CHECK=2"
				"-g2"
		)
		target_link_options(${role} PRIVATE "--preload-file" "${CMAKE_CURRENT_BINARY_DIR}/assets@/assets")
		target_link_options(${role} PRIVATE "-sEXPORTED_RUNTIME_METHODS=['ccall']")
		target_link_options(${role} PRIVATE "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free']")
	endforeach()
	target_link_libraries(server PRIVATE phoenix_network_host)
	target_link_options(server PRIVATE "SHELL:--preload-file \"${CMAKE_CURRENT_BINARY_DIR}/server-audio@/assets/audio\"")
	target_link_libraries(client PRIVATE phoenix_network_client)
	target_compile_definitions(server PRIVATE PHOENIX_SERVER=1)
	target_link_libraries(workshop PRIVATE phoenix_tools phoenix_network_client)
	target_compile_definitions(workshop PRIVATE PHOENIX_WORKSHOP=1)
	configure_file(web/index.html "${CMAKE_CURRENT_BINARY_DIR}/index.html" COPYONLY)
else()
	add_executable(native src/entry/native.cpp)
	target_link_libraries(native PRIVATE phoenix_network_lobby phoenix_network_host phoenix_network_client phoenix_tools)
	add_custom_command(
		TARGET native
		POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/assets" "$<TARGET_FILE_DIR:native>/assets"
		COMMAND
			${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/server-audio"
			"$<TARGET_FILE_DIR:native>/assets/audio"
	)
endif()

include(cmake/Networking.cmake)
add_executable(phoenix_comic_probe EXCLUDE_FROM_ALL tests/manual/comic.cpp)
target_link_libraries(phoenix_comic_probe PRIVATE phoenix_presentation)
if(EMSCRIPTEN)
	set_target_properties(phoenix_comic_probe PROPERTIES SUFFIX ".html")
	target_link_options(phoenix_comic_probe PRIVATE "-sALLOW_MEMORY_GROWTH=1" "-sASSERTIONS=1" "-sSTACK_SIZE=1048576")
endif()
add_library(phoenix_transport STATIC src/transport/options.cpp src/transport/policy.cpp)
target_link_libraries(phoenix_transport PUBLIC phoenix_foundation PRIVATE nlohmann_json::nlohmann_json)
if(EMSCRIPTEN)
	target_sources(phoenix_transport PRIVATE src/platform/browser_transport.cpp)
else()
	target_sources(phoenix_transport PRIVATE src/platform/native_transport.cpp)
	target_link_libraries(phoenix_transport PRIVATE datachannel-static crypt32)
endif()
add_executable(phoenix_network_probe tests/manual/network.cpp)
target_link_libraries(phoenix_network_probe PRIVATE phoenix_transport nlohmann_json::nlohmann_json)
if(EMSCRIPTEN)
	target_link_options(
		phoenix_network_probe
		PRIVATE
			"-sALLOW_MEMORY_GROWTH=1"
			"-sSTACK_SIZE=1048576"
			"-sEXPORTED_FUNCTIONS=['_main','_malloc','_free']"
			"-sEXPORTED_RUNTIME_METHODS=['ccall']"
	)
	configure_file(web/network-probe.html "${CMAKE_CURRENT_BINARY_DIR}/network-probe.html" COPYONLY)
endif()
add_library(phoenix_network_client STATIC src/network/client.cpp)
add_library(phoenix_fleet STATIC src/replication/fleet.cpp)
target_link_libraries(phoenix_fleet PUBLIC phoenix_persistence phoenix_wire phoenix_transport)
target_link_libraries(phoenix_network_client PUBLIC phoenix_session_api phoenix_blobs phoenix_transport phoenix_wire)
add_library(phoenix_network_host STATIC src/network/host.cpp)
add_library(phoenix_network_lobby STATIC src/network/lobby.cpp)
target_link_libraries(phoenix_network_lobby PUBLIC phoenix_network_host phoenix_network_client phoenix_fleet_setup)
if(NOT EMSCRIPTEN)
	add_executable(phoenix_lobby_tests tests/system/lobby.cpp)
	target_link_libraries(phoenix_lobby_tests PRIVATE phoenix_network_lobby)
	phoenix_fixture(phoenix_lobby_tests)
endif()
add_library(phoenix_fleet_setup STATIC src/session/fleet_setup.cpp src/session/fleet_runtime.cpp)
target_link_libraries(phoenix_fleet_setup PUBLIC phoenix_session phoenix_wire phoenix_fleet phoenix_blobs)
if(EMSCRIPTEN)
	add_executable(phoenix_fleet_setup_check tests/system/fleet_setup.cpp)
	target_link_libraries(phoenix_fleet_setup_check PRIVATE phoenix_fleet_setup)
	phoenix_fixture(phoenix_fleet_setup_check)
else()
	add_executable(phoenix_fleet_setup_tests tests/system/fleet_setup.cpp)
	target_link_libraries(phoenix_fleet_setup_tests PRIVATE phoenix_fleet_setup)
	phoenix_fixture(phoenix_fleet_setup_tests)
	add_test(NAME system.fleet-setup COMMAND phoenix_fleet_setup_tests)
	add_executable(phoenix_fleet_runtime_tests tests/system/fleet_runtime.cpp)
	target_link_libraries(phoenix_fleet_runtime_tests PRIVATE phoenix_fleet_setup)
	phoenix_fixture(phoenix_fleet_runtime_tests)
	add_test(NAME system.fleet-runtime COMMAND phoenix_fleet_runtime_tests)
endif()
target_link_libraries(
	phoenix_network_host
	PUBLIC phoenix_session phoenix_blobs phoenix_transport phoenix_persistence phoenix_wire phoenix_fleet_setup
)
if(NOT EMSCRIPTEN)
	add_executable(phoenix_fleet_probe tests/manual/fleet.cpp)
	target_link_libraries(phoenix_fleet_probe PRIVATE phoenix_fleet phoenix_simulation)
	phoenix_fixture(phoenix_fleet_probe)
	add_executable(phoenix_session_probe tests/manual/session.cpp)
	target_link_libraries(phoenix_session_probe PRIVATE phoenix_network_host phoenix_network_client)
	phoenix_fixture(phoenix_session_probe)
	add_executable(phoenix_browser_fleet_probe tests/manual/browser_fleet.cpp)
	target_link_libraries(phoenix_browser_fleet_probe PRIVATE phoenix_network_host)
	phoenix_fixture(phoenix_browser_fleet_probe)
endif()
include(cmake/Boundaries.cmake)
phoenix_forbid_dependency(phoenix_network_client phoenix_simulation "")
phoenix_forbid_dependency(phoenix_network_client flecs_static "")
foreach(forbidden raylib phoenix_presentation phoenix_tools)
	phoenix_forbid_dependency(phoenix_simulation "${forbidden}" "")
endforeach()
if(EMSCRIPTEN)
	foreach(frontend client workshop)
		foreach(forbidden phoenix_simulation flecs_static)
			phoenix_forbid_dependency("${frontend}" "${forbidden}" "")
		endforeach()
	endforeach()
endif()

if(EMSCRIPTEN)
	add_executable(phoenix_input_check tests/system/input_and_semantics.cpp)
	target_link_libraries(phoenix_input_check PRIVATE phoenix_core)
	set_property(TARGET phoenix_input_check PROPERTY CXX_SCAN_FOR_MODULES ON)
endif()

include(cmake/Modules.cmake)
include(cmake/BuildPerformance.cmake)
foreach(
	frontend
	phoenix_session_api
	phoenix_core
	phoenix_presentation
	phoenix_tools
	phoenix_network_client
)
	foreach(
		forbidden
		phoenix_session
		phoenix_network_host
		phoenix_fleet_setup
		phoenix_simulation
		flecs_static
	)
		phoenix_forbid_dependency(${frontend} ${forbidden} "")
	endforeach()
endforeach()
