cmake_minimum_required(VERSION 3.28)
project(PhoenixV3 VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static dependencies" FORCE)
include(FetchContent)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
	# Clang module producers and consumers must agree on POSIX thread support.
	# GTK also adds this flag to consumers; apply it consistently from the start.
	add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-pthread>")
	add_link_options(-pthread)
endif()

# Opt-in while the browser worker and isolated hosting path are validated.
option(PHOENIX_BROWSER_THREADS "Run browser local simulation on a shared-memory worker" OFF)
set(PHOENIX_EXCEPTION_FLAG -fexceptions)
if(EMSCRIPTEN AND PHOENIX_BROWSER_THREADS)
	set(PHOENIX_EXCEPTION_FLAG -fwasm-exceptions)
	add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-fwasm-exceptions>" "-sSUPPORT_LONGJMP=wasm")
	add_link_options(-fwasm-exceptions "-sSUPPORT_LONGJMP=wasm")
	add_compile_options(-pthread)
	add_compile_definitions(PHOENIX_BROWSER_THREADS=1)
	add_link_options(-pthread "-sPTHREAD_POOL_SIZE=1" "-sALLOW_MEMORY_GROWTH=1" "-sMALLOC=mimalloc")
endif()

function(phoenix_fixture target)
	# The shared synthetic pack helpers import their real archive/resolution owners.
	target_link_libraries(${target} PRIVATE phoenix_content_packs phoenix_content_compilers)
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
# GLFW exports the standard NVIDIA/AMD hybrid-GPU preference on Windows.
# Keep an opt-out for battery-oriented builds and record the selected GPU in captures.
option(PHOENIX_PREFER_HIGH_PERFORMANCE_GPU "Prefer the discrete GPU on hybrid Windows machines" OFF)
if(WIN32 AND NOT EMSCRIPTEN AND PHOENIX_PREFER_HIGH_PERFORMANCE_GPU)
	target_compile_definitions(glfw PRIVATE _GLFW_USE_HYBRID_HPG)
endif()

target_compile_definitions(raylib PRIVATE SUPPORT_FILEFORMAT_JPG=1)
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
	# raylib/GLFW owns the window; SDL is used only for controllers.
	set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)
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
# Exact lossless DAT pixels; identical scalar encoder on native and WASM.
set(WEBP_ENABLE_SIMD OFF CACHE BOOL "" FORCE)
set(WEBP_USE_THREAD OFF CACHE BOOL "" FORCE)
foreach(
	tool
	ANIM_UTILS
	CWEBP
	DWEBP
	GIF2WEBP
	IMG2WEBP
	VWEBP
	WEBPINFO
	WEBPMUX
	EXTRAS
)
	set(WEBP_BUILD_${tool} OFF CACHE BOOL "" FORCE)
endforeach()
FetchContent_Declare(
	webp
	URL https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz
	URL_HASH SHA256=e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(webp)
include(cmake/WebPScalar.cmake)

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
	target_compile_options(phoenix_qr PRIVATE ${PHOENIX_EXCEPTION_FLAG})
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
# Public sidecars and host-generated fallback must have identical identities on every platform.
target_compile_definitions(phoenix_zip PUBLIC MINIZ_NO_TIME)
add_library(
	phoenix_imgui
	STATIC
	${imgui_SOURCE_DIR}/imgui.cpp
	${imgui_SOURCE_DIR}/imgui_draw.cpp
	${imgui_SOURCE_DIR}/imgui_tables.cpp
	${imgui_SOURCE_DIR}/imgui_widgets.cpp
)
target_include_directories(phoenix_imgui PUBLIC ${imgui_SOURCE_DIR})
add_subdirectory(src/foundation)
add_subdirectory(src/resources/stocks)
add_subdirectory(src/resources/processors)
add_subdirectory(src/ui)
add_subdirectory(src/application/preferences)

add_subdirectory(src/content)

# Domain definitions and execution have independent module-local owners.
add_subdirectory(src/behavior)
add_subdirectory(src/knowledge/records)
add_subdirectory(src/resources/networks)
add_subdirectory(src/scenarios/environment)
add_subdirectory(src/scenarios/runtime)
add_subdirectory(src/gameplay/conversations)

add_subdirectory(src/application)
add_subdirectory(src/executables/diagnostics)

if(WIN32)
endif()

if(EMSCRIPTEN)
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
elseif(WIN32)

else()
endif()

if(NOT EMSCRIPTEN)
	add_executable(phoenix_inference_bridge src/executables/inference-bridge/inference_bridge.cpp)
	target_link_libraries(phoenix_inference_bridge PRIVATE phoenix_inference phoenix_wire)
	add_executable(phoenix_inference_probe tests/manual/inference.cpp)
	target_link_libraries(
		phoenix_inference_probe
		PRIVATE phoenix_inference phoenix_simulation phoenix_control_inference_host
	)
	phoenix_fixture(phoenix_inference_probe)
endif()

if(EMSCRIPTEN)
else()
endif()
add_library(phoenix_scenario_bridge STATIC src/scenarios/integration/gameplay_scenario.cpp)
target_sources(
	phoenix_scenario_bridge
	PUBLIC FILE_SET CXX_MODULES FILES src/scenarios/integration/gameplay_scenario.cppm
)
target_link_libraries(
	phoenix_scenario_bridge
	PUBLIC phoenix_content_catalog phoenix_content_effects_types phoenix_effects_contracts
	PRIVATE
		phoenix_content_compilers
		phoenix_scenarios_runtime
		phoenix_behavior_state_tree
		phoenix_knowledge_records_definitions
)
add_subdirectory(src/world/state)
add_subdirectory(src/world/spatial)
add_subdirectory(src/gameplay/flight)
add_subdirectory(src/control)
add_subdirectory(src/knowledge/projections)
add_subdirectory(src/knowledge/consoles)
add_subdirectory(src/knowledge/read-models)
add_subdirectory(src/effects)
add_subdirectory(src/gameplay/combat)
add_subdirectory(src/gameplay/personnel)
add_subdirectory(src/gameplay/teams)
add_subdirectory(src/resources/equipment)
add_subdirectory(src/gameplay/fitting)
add_subdirectory(src/gameplay/ftl)
add_subdirectory(src/gameplay/docking)
add_subdirectory(src/gameplay/craft)
add_subdirectory(src/gameplay/logistics)
add_subdirectory(src/gameplay/sensors)
add_subdirectory(src/gameplay/psychic)
add_subdirectory(src/gameplay/boarding)
add_subdirectory(src/gameplay/away)
add_subdirectory(src/world/persistence)
add_subdirectory(src/scenarios/objectives)
add_subdirectory(src/world/lifecycle)
add_subdirectory(src/gameplay/directed)
add_subdirectory(src/gameplay/ordnance)
add_subdirectory(src/gameplay/combat-continuation)
add_subdirectory(src/gameplay/weapons)
add_subdirectory(src/gameplay/damage)
add_subdirectory(src/gameplay/repairs)
add_subdirectory(src/gameplay/ordnance-contacts)
add_subdirectory(src/gameplay/machinery)
add_library(phoenix_simulation STATIC src/world/lifecycle/world.cpp)
target_link_libraries(
	phoenix_simulation
	PUBLIC
		phoenix_foundation
		phoenix_content_catalog
		phoenix_control_controllers
		phoenix_effects_contracts
		phoenix_effects_processes
	PRIVATE
		flecs_static
		phoenix_content_compilers
		phoenix_content_assets
		phoenix_behavior_state_tree
		phoenix_knowledge_records
		phoenix_resources_stocks
		phoenix_resources_processors
		phoenix_resources_networks
		phoenix_scenarios_runtime
		phoenix_gameplay_conversations
		phoenix_scenario_bridge
		phoenix_world_state
		phoenix_world_spatial
		phoenix_gameplay_flight
		phoenix_knowledge_consoles
		phoenix_control_providers
		phoenix_control_ownership
		phoenix_effects_rules
		phoenix_effects_rule_state
		phoenix_effects_abilities
		phoenix_effects_properties
		phoenix_effects_events
		phoenix_effects_transactions
		phoenix_gameplay_combat_state
		phoenix_gameplay_personnel_state
		phoenix_gameplay_combat_outcomes
		phoenix_gameplay_personnel_outcomes
		phoenix_gameplay_team_routes
		phoenix_resources_equipment
		phoenix_control_coordination
		phoenix_control_execution
		phoenix_knowledge_read_models
		phoenix_effects_grants
		phoenix_effects_native
		phoenix_gameplay_combat_shields
		phoenix_gameplay_combat_faults
		phoenix_gameplay_damage
		phoenix_gameplay_repairs
		phoenix_gameplay_ordnance_contacts
		phoenix_gameplay_fitting
		phoenix_gameplay_ftl
		phoenix_gameplay_docking
		phoenix_gameplay_craft
		phoenix_gameplay_logistics_state
)
if(MSVC)
	target_compile_options(phoenix_simulation PRIVATE /bigobj)
endif()
add_library(phoenix_workshop_test_host STATIC src/executables/workshop-host/workshop_test.cpp)
target_sources(
	phoenix_workshop_test_host
	PUBLIC FILE_SET CXX_MODULES FILES src/executables/workshop-host/workshop_test.cppm
)
target_link_libraries(phoenix_workshop_test_host PUBLIC phoenix_simulation PRIVATE phoenix_content_compilers)

add_subdirectory(src/graphics)
add_library(phoenix_presentation STATIC src/ui/runtime/screen.cpp)
target_sources(phoenix_presentation PRIVATE src/ui/runtime/accessibility.cpp)
target_link_libraries(
	phoenix_presentation
	PUBLIC
		phoenix_graphics_assets
		phoenix_graphics_scene
		phoenix_graphics_radar
		phoenix_graphics_audio
		phoenix_ui_rml
		phoenix_knowledge_read_models
		phoenix_ui_console_surface
		phoenix_ui_viewscreen
		phoenix_ui_screens
		phoenix_ui_settings
		phoenix_ui_theme
		phoenix_ui_engineering_widgets
		phoenix_ui_information_widgets
		phoenix_ui_preview
		phoenix_accessibility
)

if(EMSCRIPTEN)
else()

	if(WIN32)
	endif()
endif()
foreach(
	case
	IN
	ITEMS
		gameplay_knowledge
		processors
		store_effects
		directed
		gameplay_flight
		scenario_world
		summary_bindings
		swept_collision
		workshop_test
		personnel
		medical
		logistics
		docking
		cargo
		security
		away
		craft
		ftl
		sensors
		psychic
		abilities
		rules
		custom_properties
		processes
		controller_runtime
		combat
)
	add_executable(phoenix_${case}_tests tests/system/${case}.cpp)
	target_link_libraries(phoenix_${case}_tests PRIVATE phoenix_simulation phoenix_workshop_test_host phoenix_session)
	phoenix_fixture(phoenix_${case}_tests)
	if(NOT EMSCRIPTEN)
		add_test(NAME system.${case} COMMAND phoenix_${case}_tests)
	endif()
endforeach()
target_sources(phoenix_gameplay_flight_tests PRIVATE tests/support/resource_allocation.cpp)
add_executable(phoenix_conversation_tests tests/system/conversations.cpp)
target_link_libraries(phoenix_conversation_tests PRIVATE phoenix_simulation)
phoenix_fixture(phoenix_conversation_tests)
if(NOT EMSCRIPTEN)
	add_test(NAME system.conversations COMMAND phoenix_conversation_tests)
endif()
add_executable(phoenix_scenario_tests tests/manual/scenario.cpp)
target_link_libraries(
	phoenix_scenario_tests
	PRIVATE phoenix_scenario_bridge phoenix_scenarios_runtime phoenix_behavior_state_tree
)
phoenix_fixture(phoenix_scenario_tests)
if(NOT EMSCRIPTEN)
	add_test(NAME system.scenario-situations COMMAND phoenix_scenario_tests)
endif()
add_executable(phoenix_gameplay_tests tests/manual/gameplay.cpp tests/support/stock_accounting.cpp)
target_link_libraries(
	phoenix_gameplay_tests
	PRIVATE
		phoenix_content_documents
		phoenix_content_compilers
		phoenix_content_packs
		phoenix_resources_networks
		phoenix_resources_stocks
		phoenix_knowledge_records
		phoenix_behavior_state_tree
)
phoenix_fixture(phoenix_gameplay_tests)
if(NOT EMSCRIPTEN)
	add_test(NAME system.gameplay-foundations COMMAND phoenix_gameplay_tests)
endif()
if(EMSCRIPTEN)
else()
	enable_testing()
	add_executable(phoenix_menu_tests tests/system/menu.cpp)
	target_link_libraries(phoenix_menu_tests PRIVATE phoenix_application_library phoenix_session)
	phoenix_fixture(phoenix_menu_tests)
	add_test(NAME system.native-menu-library COMMAND phoenix_menu_tests)
	add_executable(phoenix_system_tests tests/system/input_and_semantics.cpp)
	target_link_libraries(phoenix_system_tests PRIVATE phoenix_ui_navigation phoenix_console_model)
	add_test(NAME system.input-and-semantics COMMAND phoenix_system_tests)
	add_executable(phoenix_content_tests tests/system/content.cpp)
	target_link_libraries(
		phoenix_content_tests
		PRIVATE
			phoenix_content_assets
			phoenix_content_catalog
			phoenix_content_compilers
			phoenix_content_distribution
			phoenix_content_documents
			phoenix_content_image_codec
			phoenix_content_packs
			phoenix_content_ui
			phoenix_scenarios_runtime
	)
	phoenix_fixture(phoenix_content_tests)
	add_test(NAME system.content-and-packs COMMAND phoenix_content_tests)
	add_executable(phoenix_simulation_tests tests/system/simulation.cpp)
	target_link_libraries(phoenix_simulation_tests PRIVATE phoenix_simulation)
	phoenix_fixture(phoenix_simulation_tests)
	add_test(NAME system.motion-radar-trees COMMAND phoenix_simulation_tests)
	add_executable(phoenix_session_tests tests/system/session.cpp)
	target_link_libraries(phoenix_session_tests PRIVATE phoenix_session phoenix_ui_navigation phoenix_console_model)
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
	target_link_libraries(phoenix_store_tests PRIVATE phoenix_persistence phoenix_foundation)
	add_test(NAME system.peer-storage COMMAND phoenix_store_tests)
	add_executable(phoenix_transfer_tests tests/system/network_content.cpp)
	target_link_libraries(phoenix_transfer_tests PRIVATE phoenix_wire)
	phoenix_fixture(phoenix_transfer_tests)
	add_test(NAME system.network-content COMMAND phoenix_transfer_tests)
	add_executable(phoenix_blob_tests tests/system/blob_store.cpp)
	target_link_libraries(phoenix_blob_tests PRIVATE phoenix_blobs phoenix_wire)
	phoenix_fixture(phoenix_blob_tests)
	add_test(NAME system.persistent-content COMMAND phoenix_blob_tests)
	add_executable(phoenix_verify src/executables/verify/verify.cpp)
	target_link_libraries(phoenix_verify PRIVATE phoenix_content_packs phoenix_content_compilers)
	add_executable(phoenix_pack src/executables/pack/pack.cpp)
	target_link_libraries(
		phoenix_pack
		PRIVATE phoenix_content_packs phoenix_content_catalog phoenix_content_distribution phoenix_content_compilers
	)
	# Track archive contents and membership, including removed files. The list file
	# changes only when membership changes, so reconfiguration alone does not repack.
	file(
		GLOB_RECURSE phoenix_base_sources
		LIST_DIRECTORIES FALSE
		CONFIGURE_DEPENDS
		"${CMAKE_CURRENT_SOURCE_DIR}/content/base/*"
	)
	file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/base-pack-inputs.txt" CONTENT "${phoenix_base_sources}" @ONLY)
	add_custom_command(
		OUTPUT
			"${CMAKE_CURRENT_BINARY_DIR}/base.zip"
			"${CMAKE_CURRENT_BINARY_DIR}/base.zip.menu.json"
			"${CMAKE_CURRENT_BINARY_DIR}/assets/base.zip"
		COMMAND phoenix_pack "${CMAKE_CURRENT_SOURCE_DIR}/content/base" "${CMAKE_CURRENT_BINARY_DIR}/base.zip"
		COMMAND
			${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/base.zip"
			"${CMAKE_CURRENT_BINARY_DIR}/assets/base.zip"
		DEPENDS phoenix_pack ${phoenix_base_sources} "${CMAKE_CURRENT_BINARY_DIR}/base-pack-inputs.txt"
		VERBATIM
	)
	add_custom_target(
		base_pack
		ALL
		COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets"
		COMMAND
			${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/base.zip"
			"${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/base.zip"
		COMMAND
			${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/base.zip.menu.json"
			"${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/base.zip.menu.json"
		COMMAND
			${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/base.zip.menu.json"
			"${CMAKE_CURRENT_BINARY_DIR}/assets/base.zip.menu.json"
		COMMAND
			${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/base.zip.clients"
			"${CMAKE_CURRENT_BINARY_DIR}/assets/base.zip.clients"
		COMMAND
			${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/base.zip.clients"
			"${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/base.zip.clients"
		DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/base.zip" "${CMAKE_CURRENT_BINARY_DIR}/assets/base.zip"
	)
	file(GLOB_RECURSE mission_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/content/test-missions/*")
	file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/test-missions-inputs.txt" CONTENT "${mission_sources}" @ONLY)
	add_custom_command(
		OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/test-missions.zip" "${CMAKE_CURRENT_BINARY_DIR}/test-missions.zip.menu.json"
		COMMAND
			phoenix_pack "${CMAKE_CURRENT_SOURCE_DIR}/content/test-missions" "${CMAKE_CURRENT_BINARY_DIR}/test-missions.zip"
			"${CMAKE_CURRENT_BINARY_DIR}/base.zip"
		DEPENDS
			phoenix_pack
			${mission_sources}
			"${CMAKE_CURRENT_BINARY_DIR}/test-missions-inputs.txt"
			"${CMAKE_CURRENT_BINARY_DIR}/base.zip"
		VERBATIM
	)
	add_custom_target(
		test_missions_pack
		ALL
		COMMAND
			${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/test-missions.zip"
			"${CMAKE_CURRENT_BINARY_DIR}/assets/test-missions.zip"
		COMMAND
			${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/test-missions.zip"
			"${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/test-missions.zip"
		COMMAND
			${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/test-missions.zip.menu.json"
			"${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/test-missions.zip.menu.json"
		COMMAND
			${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_BINARY_DIR}/test-missions.zip.menu.json"
			"${CMAKE_CURRENT_BINARY_DIR}/assets/test-missions.zip.menu.json"
		COMMAND
			${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/test-missions.zip.clients"
			"${CMAKE_CURRENT_BINARY_DIR}/assets/test-missions.zip.clients"
		COMMAND
			${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/test-missions.zip.clients"
			"${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assets/test-missions.zip.clients"
		DEPENDS base_pack "${CMAKE_CURRENT_BINARY_DIR}/test-missions.zip"
	)
	foreach(test_base IN ITEMS test test-3d)
		file(
			GLOB_RECURSE test_sources
			LIST_DIRECTORIES FALSE
			CONFIGURE_DEPENDS
			"${CMAKE_CURRENT_SOURCE_DIR}/content/${test_base}/*"
		)
		file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${test_base}-inputs.txt" CONTENT "${test_sources}" @ONLY)
		add_custom_command(
			OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${test_base}.zip" "${CMAKE_CURRENT_BINARY_DIR}/${test_base}.zip.menu.json"
			COMMAND
				phoenix_pack "${CMAKE_CURRENT_SOURCE_DIR}/content/${test_base}" "${CMAKE_CURRENT_BINARY_DIR}/${test_base}.zip"
			DEPENDS phoenix_pack ${test_sources} "${CMAKE_CURRENT_BINARY_DIR}/${test_base}-inputs.txt"
			VERBATIM
		)
		string(REPLACE "-" "_" test_target "${test_base}")
		add_custom_target(${test_target}_pack DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${test_base}.zip")
	endforeach()
	add_custom_target(development_packs DEPENDS test_pack test_3d_pack)

	if(WIN32)
		target_link_libraries(
			phoenix_presentation
			PRIVATE uiautomationcore ole32 oleaut32 comctl32 imm32
		)
	else()
	endif()
endif()

target_link_libraries(
	phoenix_presentation
	PUBLIC phoenix_application_shell phoenix_files raylib RmlUi::RmlUi
	PRIVATE phoenix_ui_navigation phoenix_console_model phoenix_qr phoenix_devices phoenix_shaping phoenix_content_packs
)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/assets")
# Retire archives left by older builds before any asset directory is preloaded.
file(
	REMOVE
	"${CMAKE_CURRENT_BINARY_DIR}/assets/training.zip"
	"${CMAKE_CURRENT_BINARY_DIR}/assets/test.zip"
	"${CMAKE_CURRENT_BINARY_DIR}/assets/test-3d.zip"
)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/assets/pfx")
foreach(mote space_mote_streak_head.png space_mote_streak_soft.png space_mote_compact_core.png)
	configure_file("content/pfx/${mote}" "${CMAKE_CURRENT_BINARY_DIR}/assets/pfx/${mote}" COPYONLY)
endforeach()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/assets/helm")
file(
	GLOB phoenix_helm_art
	CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/content/ui/helm/*.png"
	"${CMAKE_CURRENT_SOURCE_DIR}/content/ui/helm/*.json"
)
foreach(asset IN LISTS phoenix_helm_art)
	get_filename_component(name "${asset}" NAME)
	configure_file("${asset}" "${CMAKE_CURRENT_BINARY_DIR}/assets/helm/${name}" COPYONLY)
endforeach()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/assets/menu")
foreach(
	menu_asset
	IN
	ITEMS
		button-generic-9slice.png
		button-generic-9slice.sprite.json
		helm-background-9slice.png
		helm-background-9slice.sprite.json
		lobby-panel-9slice.png
		lobby-panel-9slice.sprite.json
		sprite-set.schema.json
		settings.png
		star-full.png
		star-empty.png
		background.png
		cockpit-frame.png
		space-panorama.png
		menu-debris.png
		menu-logotype.png
		nebula-flash-mask.png
		crt-terminal.png
		scutter.png
		menu-scene.json
)
	configure_file("content/ui/menu/${menu_asset}" "${CMAKE_CURRENT_BINARY_DIR}/assets/menu/${menu_asset}" COPYONLY)
endforeach()
foreach(avatar RANGE 1 12)
	configure_file(
		"content/ui/menu/portrait-${avatar}.png"
		"${CMAKE_CURRENT_BINARY_DIR}/assets/menu/portrait-${avatar}.png"
		COPYONLY
	)
	configure_file(
		"content/ui/menu/avatar-${avatar}.png"
		"${CMAKE_CURRENT_BINARY_DIR}/assets/menu/avatar-${avatar}.png"
		COPYONLY
	)
endforeach()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/assets/messages")
foreach(portrait RANGE 1 6)
	configure_file(
		"content/ui/messages/console-${portrait}.png"
		"${CMAKE_CURRENT_BINARY_DIR}/assets/messages/console-${portrait}.png"
		COPYONLY
	)
endforeach()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/server-audio")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/assets/audio")
foreach(audio_file IN ITEMS ui_click.ogg CREDITS.txt)
	configure_file("content/audio/${audio_file}" "${CMAKE_CURRENT_BINARY_DIR}/assets/audio/${audio_file}" COPYONLY)
endforeach()
foreach(audio_file IN ITEMS menu.ogg ambient.ogg CREDITS.txt)
	configure_file("content/audio/${audio_file}" "${CMAKE_CURRENT_BINARY_DIR}/server-audio/${audio_file}" COPYONLY)
endforeach()
configure_file("content/locales.json" "${CMAKE_CURRENT_BINARY_DIR}/assets/locales.json" COPYONLY)
foreach(
	title_font_asset
	UnifrakturCook-Bold.ttf
	UnifrakturCook-OFL.txt
	SpaceMono-Regular.ttf
	SpaceMono-OFL.txt
	ComicNeue-Regular.ttf
	ComicNeue-OFL.txt
)
	configure_file("content/fonts/${title_font_asset}" "${CMAKE_CURRENT_BINARY_DIR}/assets/${title_font_asset}" COPYONLY)
endforeach()
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
configure_file("${webp_SOURCE_DIR}/COPYING" "${CMAKE_CURRENT_BINARY_DIR}/assets/WEBP-LICENSE.txt" COPYONLY)
configure_file("${webp_SOURCE_DIR}/PATENTS" "${CMAKE_CURRENT_BINARY_DIR}/assets/WEBP-PATENTS.txt" COPYONLY)
add_subdirectory(src/workshop)

if(EMSCRIPTEN)
	add_executable(phoenix_workshop_test src/executables/workshop-host/main.cpp)
	target_link_libraries(phoenix_workshop_test PRIVATE phoenix_workshop_test_host phoenix_application_library)
	target_link_options(
		phoenix_workshop_test
		PRIVATE
			"-sMODULARIZE=1"
			"-sEXPORT_NAME=createPhoenixWorkshopTestModule"
			"-sSTACK_SIZE=1048576"
			"-sENVIRONMENT=worker,node"
			"-sALLOW_MEMORY_GROWTH=1"
			"-sEXIT_RUNTIME=0"
			"-sEXPORTED_RUNTIME_METHODS=['ccall','HEAPU8']"
			"-sEXPORTED_FUNCTIONS=['_main','_malloc','_free']"
	)
	configure_file(web/workshop-test-worker.js "${CMAKE_CURRENT_BINARY_DIR}/workshop-test-worker.js" COPYONLY)
endif()

if(EMSCRIPTEN)
elseif(WIN32)

else()
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
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
	add_executable(phoenix_build_check src/executables/diagnostics/build_check.cpp)
	target_link_libraries(phoenix_build_check PRIVATE phoenix_simulation)
	target_link_options(
		phoenix_build_check
		PRIVATE "-sALLOW_MEMORY_GROWTH=1" "-sASSERTIONS=2" "-sSTACK_SIZE=1048576" "-sSTACK_OVERFLOW_CHECK=2" "-g2"
	)
	add_executable(phoenix_content_check tests/system/content.cpp)
	target_link_libraries(
		phoenix_content_check
		PRIVATE
			phoenix_content_assets
			phoenix_content_catalog
			phoenix_content_compilers
			phoenix_content_distribution
			phoenix_content_documents
			phoenix_content_image_codec
			phoenix_content_packs
			phoenix_content_ui
			phoenix_scenarios_runtime
	)
	phoenix_fixture(phoenix_content_check)
	add_executable(phoenix_transfer_check tests/system/network_content.cpp)
	target_link_libraries(phoenix_transfer_check PRIVATE phoenix_wire)
	phoenix_fixture(phoenix_transfer_check)
	add_executable(phoenix_replication_check tests/system/replication.cpp)
	target_link_libraries(phoenix_replication_check PRIVATE phoenix_replication phoenix_simulation)
	phoenix_fixture(phoenix_replication_check)
	add_executable(phoenix_session_check tests/system/session.cpp)
	target_link_libraries(phoenix_session_check PRIVATE phoenix_session phoenix_ui_navigation phoenix_console_model)
	phoenix_fixture(phoenix_session_check)
	add_executable(phoenix_file_check tests/system/file_imports.cpp)
	target_link_libraries(phoenix_file_check PRIVATE phoenix_files phoenix_session)
	phoenix_fixture(phoenix_file_check)
	add_executable(phoenix_simulation_check tests/system/simulation.cpp)
	target_link_libraries(phoenix_simulation_check PRIVATE phoenix_simulation)
	phoenix_fixture(phoenix_simulation_check)
	foreach(role IN ITEMS server client workshop)
		add_executable(${role} src/executables/browser/web.cpp)
		target_compile_definitions(${role} PRIVATE PHOENIX_ROLE="${role}")
		target_link_libraries(${role} PRIVATE phoenix_presentation)
		target_link_options(
			${role}
			PRIVATE
				"-sALLOW_MEMORY_GROWTH=1"
				"-sASSERTIONS=1"
				"-sFORCE_FILESYSTEM=1"
				"-sSTACK_SIZE=1048576"
				"$<$<CONFIG:Release>:-sSTACK_OVERFLOW_CHECK=1>"
				"$<$<NOT:$<CONFIG:Release>>:-sSTACK_OVERFLOW_CHECK=2>"
				"-g2"
		)
		# Crew clients receive gameplay presentation content from their ship server.
		# Do not hide full installed game packs inside the common WASM .data file.
		if(role STREQUAL "client")
			file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/client-assets")
			file(
				COPY "${CMAKE_CURRENT_BINARY_DIR}/assets/"
				DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/client-assets"
				PATTERN "*.zip"
				EXCLUDE
				PATTERN "*.dat"
				EXCLUDE
			)
			target_link_options(${role} PRIVATE "--preload-file" "${CMAKE_CURRENT_BINARY_DIR}/client-assets@/assets")
		else()
			target_link_options(${role} PRIVATE "--preload-file" "${CMAKE_CURRENT_BINARY_DIR}/assets@/assets")
		endif()
		target_link_options(${role} PRIVATE "-sEXPORTED_RUNTIME_METHODS=['ccall','FS']")
		target_link_options(${role} PRIVATE "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free']")
	endforeach()
	# raylib exports ccall publicly; avoid its later option replacing the shell's FS export.
	get_target_property(phoenix_raylib_link_options raylib INTERFACE_LINK_OPTIONS)
	list(FILTER phoenix_raylib_link_options EXCLUDE REGEX "^-sEXPORTED_RUNTIME_METHODS=")
	set_property(TARGET raylib PROPERTY INTERFACE_LINK_OPTIONS "${phoenix_raylib_link_options}")
	target_link_libraries(server PRIVATE phoenix_network_lobby phoenix_tools)
	target_link_options(server PRIVATE "SHELL:--preload-file \"${CMAKE_CURRENT_BINARY_DIR}/server-audio@/assets/audio\"")
	target_link_libraries(client PRIVATE phoenix_lobby_protocol)
	target_compile_definitions(server PRIVATE PHOENIX_SERVER=1)
	target_link_libraries(workshop PRIVATE phoenix_tools phoenix_lobby_protocol)
	# Workshop can open the shipped base directly, including without launcher state.
	configure_file("${CMAKE_CURRENT_SOURCE_DIR}/build/native/base.zip" "${CMAKE_CURRENT_BINARY_DIR}/base.zip" COPYONLY)

	target_link_options(workshop PRIVATE "SHELL:--preload-file \"${CMAKE_CURRENT_BINARY_DIR}/base.zip@/assets/base.zip\"")
	set_property(TARGET workshop APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/base.zip")
	target_compile_definitions(workshop PRIVATE PHOENIX_WORKSHOP=1)
	configure_file(web/index.html "${CMAKE_CURRENT_BINARY_DIR}/index.html" COPYONLY)
	configure_file(web/menu.css "${CMAKE_CURRENT_BINARY_DIR}/menu.css" COPYONLY)
	configure_file(web/launcher.mjs "${CMAKE_CURRENT_BINARY_DIR}/launcher.mjs" COPYONLY)
	configure_file(web/runtime-environment.mjs "${CMAKE_CURRENT_BINARY_DIR}/runtime-environment.mjs" COPYONLY)
	configure_file(web/isolation-worker.js "${CMAKE_CURRENT_BINARY_DIR}/isolation-worker.js" COPYONLY)
	set(PHOENIX_ROOT_THREADED false)
	if(PHOENIX_BROWSER_THREADS)
		set(PHOENIX_ROOT_THREADED true)
	endif()
	configure_file(web/runtime-capabilities.json.in "${CMAKE_CURRENT_BINARY_DIR}/runtime-capabilities.json" @ONLY)

	configure_file(web/base-verifier.mjs "${CMAKE_CURRENT_BINARY_DIR}/base-verifier.mjs" COPYONLY)
	configure_file(web/base-verifier-worker.js "${CMAKE_CURRENT_BINARY_DIR}/base-verifier-worker.js" COPYONLY)
	configure_file(web/session-pump.mjs "${CMAKE_CURRENT_BINARY_DIR}/session-pump.mjs" COPYONLY)
	configure_file(web/session-profile.mjs "${CMAKE_CURRENT_BINARY_DIR}/session-profile.mjs" COPYONLY)
	configure_file(web/session-clock.js "${CMAKE_CURRENT_BINARY_DIR}/session-clock.js" COPYONLY)
else()
	add_subdirectory(src/executables/native)
	add_executable(native src/executables/native/native.cpp tests/manual/native_smokes.cpp)
	target_link_libraries(
		native
		PRIVATE
			phoenix_native_displays
			phoenix_network_lobby
			phoenix_network_host
			phoenix_network_client
			phoenix_tools
			phoenix_workshop_test_host
			phoenix_content_compilers
			phoenix_content_distribution
			phoenix_presentation
			phoenix_graphics_audio
			raylib
	)
	# Content and UI-only edits do not relink native. Always stage its runtime
	# dependencies so an incremental IDE launch consumes the current build.
	add_custom_target(
		native_runtime_assets
		COMMAND
			${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:native>/assets/training.zip" "$<TARGET_FILE_DIR:native>/assets/test.zip"
			"$<TARGET_FILE_DIR:native>/assets/test-3d.zip"
		COMMAND
			${CMAKE_COMMAND} -E copy_directory_if_different "${CMAKE_CURRENT_BINARY_DIR}/assets"
			"$<TARGET_FILE_DIR:native>/assets"
		COMMAND
			${CMAKE_COMMAND} -E copy_directory_if_different "${CMAKE_CURRENT_BINARY_DIR}/server-audio"
			"$<TARGET_FILE_DIR:native>/assets/audio"
		COMMENT "Staging current native packs, UI and audio"
	)
	add_dependencies(native_runtime_assets base_pack test_missions_pack)
	add_dependencies(native native_runtime_assets)
endif()

include(cmake/Networking.cmake)
add_subdirectory(src/networking)
add_subdirectory(src/platform)
add_executable(phoenix_comic_probe EXCLUDE_FROM_ALL tests/manual/comic.cpp)
target_link_libraries(phoenix_comic_probe PRIVATE phoenix_presentation)
if(NOT EMSCRIPTEN)
	add_executable(phoenix_layout_probe EXCLUDE_FROM_ALL tests/manual/layout_workshop.cpp)
	target_link_libraries(phoenix_layout_probe PRIVATE phoenix_tools)
	add_custom_command(
		TARGET phoenix_layout_probe
		POST_BUILD
		COMMAND
			${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/assets"
			"$<TARGET_FILE_DIR:phoenix_layout_probe>/assets"
	)
endif()
if(EMSCRIPTEN)
	set_target_properties(phoenix_comic_probe PROPERTIES SUFFIX ".html")
	target_link_options(phoenix_comic_probe PRIVATE "-sALLOW_MEMORY_GROWTH=1" "-sASSERTIONS=1" "-sSTACK_SIZE=1048576")
	target_link_options(phoenix_comic_probe PRIVATE "--preload-file" "${CMAKE_CURRENT_BINARY_DIR}/assets/pfx@/assets/pfx")
else()
	add_custom_command(
		TARGET phoenix_comic_probe
		POST_BUILD
		COMMAND
			${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/assets/pfx"
			"$<TARGET_FILE_DIR:phoenix_comic_probe>/assets/pfx"
	)
endif()

if(EMSCRIPTEN)
else()

	if(WIN32)
	else()
		find_package(CURL REQUIRED)
	endif()
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

if(NOT EMSCRIPTEN)
	add_executable(phoenix_lobby_tests tests/system/lobby.cpp)
	target_link_libraries(
		phoenix_lobby_tests
		PRIVATE phoenix_network_lobby phoenix_application_displays phoenix_console_model
	)
	phoenix_fixture(phoenix_lobby_tests)
	add_test(NAME system.lobby-worker-consoles COMMAND phoenix_lobby_tests --local-consoles)
endif()

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
phoenix_forbid_dependency(phoenix_lobby_protocol phoenix_simulation "")
phoenix_forbid_dependency(phoenix_network_client flecs_static "")
foreach(forbidden raylib phoenix_presentation phoenix_tools)
	phoenix_forbid_dependency(phoenix_simulation "${forbidden}" "")
endforeach()
if(EMSCRIPTEN)
	foreach(frontend client workshop)
		foreach(
			forbidden
			phoenix_simulation
			phoenix_behavior_state_tree
			phoenix_knowledge_records
			phoenix_resources_stocks
			phoenix_resources_processors
			phoenix_resources_networks
			phoenix_scenarios_runtime
			phoenix_gameplay_conversations
			phoenix_scenario_bridge
			flecs_static
		)
			phoenix_forbid_dependency("${frontend}" "${forbidden}" "")
		endforeach()
	endforeach()
endif()

if(EMSCRIPTEN)
	add_executable(phoenix_input_check tests/system/input_and_semantics.cpp)
	target_link_libraries(phoenix_input_check PRIVATE phoenix_ui_navigation phoenix_console_model)
	set_property(TARGET phoenix_input_check PROPERTY CXX_SCAN_FOR_MODULES ON)
endif()

add_executable(phoenix_camera_tests tests/system/camera.cpp)
target_link_libraries(phoenix_camera_tests PRIVATE phoenix_session phoenix_console_model)
phoenix_fixture(phoenix_camera_tests)
add_executable(phoenix_missions_tests tests/system/missions.cpp)
target_link_libraries(phoenix_missions_tests PRIVATE phoenix_simulation)
phoenix_fixture(phoenix_missions_tests)
add_executable(phoenix_interdictions_tests tests/system/interdictions.cpp)
target_link_libraries(phoenix_interdictions_tests PRIVATE phoenix_simulation)
phoenix_fixture(phoenix_interdictions_tests)
if(NOT EMSCRIPTEN)
	add_test(NAME system.missions COMMAND phoenix_missions_tests)
	add_test(NAME system.interdictions COMMAND phoenix_interdictions_tests)
endif()
add_executable(phoenix_sensors_smoke EXCLUDE_FROM_ALL tests/manual/sensors_ui.cpp)
target_link_libraries(phoenix_sensors_smoke PRIVATE phoenix_session phoenix_presentation)
if(NOT EMSCRIPTEN)
	add_dependencies(phoenix_sensors_smoke base_pack)
endif()
if(EMSCRIPTEN)
	set_target_properties(phoenix_sensors_smoke PROPERTIES SUFFIX ".html")
	target_link_options(
		phoenix_sensors_smoke
		PRIVATE
			"-O1"
			"-sALLOW_MEMORY_GROWTH=1"
			"-sSTACK_SIZE=1048576"
			"-sSTACK_OVERFLOW_CHECK=2"
			"--preload-file"
			"${CMAKE_CURRENT_BINARY_DIR}/assets@/assets"
			"SHELL:--preload-file \"${CMAKE_CURRENT_BINARY_DIR}/base.zip@/assets/base.zip\""
			"--shell-file"
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/manual/sensors_ui.html"
	)
	set_target_properties(
		phoenix_sensors_smoke
		PROPERTIES
			LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/base.zip;${CMAKE_CURRENT_SOURCE_DIR}/tests/manual/sensors_ui.html"
	)
endif()

add_executable(phoenix_mission_smoke EXCLUDE_FROM_ALL tests/manual/mission_ui.cpp)
target_link_libraries(phoenix_mission_smoke PRIVATE phoenix_session phoenix_presentation)
if(NOT EMSCRIPTEN)
	add_dependencies(phoenix_mission_smoke base_pack)
	add_custom_command(
		TARGET phoenix_mission_smoke
		POST_BUILD
		COMMAND
			${CMAKE_COMMAND} -E copy_directory_if_different "${CMAKE_CURRENT_BINARY_DIR}/assets"
			"$<TARGET_FILE_DIR:phoenix_mission_smoke>/assets"
		COMMAND
			${CMAKE_COMMAND} -E copy_directory_if_different "${CMAKE_CURRENT_BINARY_DIR}/server-audio"
			"$<TARGET_FILE_DIR:phoenix_mission_smoke>/assets/audio"
	)
endif()
if(EMSCRIPTEN)
	set_target_properties(phoenix_mission_smoke PROPERTIES SUFFIX ".html")
	target_link_options(
		phoenix_mission_smoke
		PRIVATE
			"-O2"
			"-sASYNCIFY=1"
			"-sASYNCIFY_STACK_SIZE=65536"
			"-sALLOW_MEMORY_GROWTH=1"
			"-sSTACK_SIZE=1048576"
			"-sSTACK_OVERFLOW_CHECK=2"
			"--preload-file"
			"${CMAKE_CURRENT_BINARY_DIR}/assets@/assets"
			"SHELL:--preload-file \"${CMAKE_CURRENT_BINARY_DIR}/base.zip@/assets/base.zip\""
			"--shell-file"
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/manual/mission_ui.html"
	)
	set_target_properties(
		phoenix_mission_smoke
		PROPERTIES
			LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/base.zip;${CMAKE_CURRENT_SOURCE_DIR}/tests/manual/mission_ui.html"
	)
endif()

add_executable(phoenix_craft_smoke EXCLUDE_FROM_ALL tests/manual/craft_ui.cpp)
target_link_libraries(phoenix_craft_smoke PRIVATE phoenix_session phoenix_presentation)
if(NOT EMSCRIPTEN)
	add_dependencies(phoenix_craft_smoke development_packs)
	add_custom_command(
		TARGET phoenix_craft_smoke
		POST_BUILD
		COMMAND
			${CMAKE_COMMAND} -E copy_directory_if_different "${CMAKE_CURRENT_BINARY_DIR}/assets"
			"$<TARGET_FILE_DIR:phoenix_craft_smoke>/assets"
		COMMAND
			${CMAKE_COMMAND} -E copy_directory_if_different "${CMAKE_CURRENT_BINARY_DIR}/server-audio"
			"$<TARGET_FILE_DIR:phoenix_craft_smoke>/assets/audio"
	)
endif()

add_executable(phoenix_camera_smoke EXCLUDE_FROM_ALL tests/manual/camera_ui.cpp)
target_link_libraries(phoenix_camera_smoke PRIVATE phoenix_session phoenix_presentation)
if(EMSCRIPTEN)
	if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/native/test.zip")
		configure_file("${CMAKE_CURRENT_SOURCE_DIR}/build/native/test.zip" "${CMAKE_CURRENT_BINARY_DIR}/test.zip" COPYONLY)
	endif()
	target_link_options(
		phoenix_camera_smoke
		PRIVATE "SHELL:--preload-file \"${CMAKE_CURRENT_BINARY_DIR}/test.zip@/assets/test.zip\""
	)
	set_target_properties(phoenix_camera_smoke PROPERTIES SUFFIX ".html")
	target_link_options(
		phoenix_camera_smoke
		PRIVATE
			"-O1"
			"-sEXPORTED_RUNTIME_METHODS=['ccall']"
			"-sALLOW_MEMORY_GROWTH=1"
			"-sSTACK_SIZE=1048576"
			"-sSTACK_OVERFLOW_CHECK=2"
			"--preload-file"
			"${CMAKE_CURRENT_BINARY_DIR}/assets@/assets"
			"SHELL:--preload-file \"${CMAKE_CURRENT_BINARY_DIR}/base.zip@/assets/base.zip\""
			"--shell-file"
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/manual/camera_ui.html"
	)
	set_property(
		TARGET phoenix_camera_smoke
		APPEND
		PROPERTY
			LINK_DEPENDS
				"${CMAKE_CURRENT_BINARY_DIR}/base.zip"
				"${CMAKE_CURRENT_BINARY_DIR}/test.zip"
				"${CMAKE_CURRENT_SOURCE_DIR}/tests/manual/camera_ui.html"
	)
else()
	add_test(NAME system.ship-camera COMMAND phoenix_camera_tests)
endif()

add_executable(phoenix_manual_flight_smoke EXCLUDE_FROM_ALL tests/manual/manual_flight_ui.cpp)
target_link_libraries(phoenix_manual_flight_smoke PRIVATE phoenix_session phoenix_presentation)
if(EMSCRIPTEN)
	set_target_properties(phoenix_manual_flight_smoke PROPERTIES SUFFIX ".html")
	target_link_options(
		phoenix_manual_flight_smoke
		PRIVATE
			"-O1"
			"-sEXPORTED_RUNTIME_METHODS=['ccall']"
			"-sALLOW_MEMORY_GROWTH=1"
			"-sSTACK_SIZE=1048576"
			"-sSTACK_OVERFLOW_CHECK=2"
			"--preload-file"
			"${CMAKE_CURRENT_BINARY_DIR}/assets@/assets"
			"SHELL:--preload-file \"${CMAKE_CURRENT_BINARY_DIR}/base.zip@/assets/base.zip\""
			"--shell-file"
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/manual/manual_flight_ui.html"
	)
	set_property(
		TARGET phoenix_manual_flight_smoke
		APPEND
		PROPERTY
			LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/base.zip" "${CMAKE_CURRENT_SOURCE_DIR}/tests/manual/manual_flight_ui.html"
	)
else()
	add_dependencies(phoenix_manual_flight_smoke base_pack)
	add_custom_command(
		TARGET phoenix_manual_flight_smoke
		POST_BUILD
		COMMAND
			${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/assets"
			"$<TARGET_FILE_DIR:phoenix_manual_flight_smoke>/assets"
	)
endif()

# Explicit visual evidence only; never registered as an automatic test.
add_executable(phoenix_messages_smoke EXCLUDE_FROM_ALL tests/manual/messages_ui.cpp)
target_link_libraries(phoenix_messages_smoke PRIVATE phoenix_presentation)
if(EMSCRIPTEN)
	set_target_properties(phoenix_messages_smoke PROPERTIES SUFFIX ".html")
	target_link_options(
		phoenix_messages_smoke
		PRIVATE "-sALLOW_MEMORY_GROWTH=1" "-sSTACK_SIZE=1048576" "--preload-file" "${CMAKE_CURRENT_BINARY_DIR}/assets@/assets"
	)
else()
	add_custom_command(
		TARGET phoenix_messages_smoke
		POST_BUILD
		COMMAND
			${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/assets"
			"$<TARGET_FILE_DIR:phoenix_messages_smoke>/assets"
	)
endif()

add_executable(phoenix_lobby_smoke EXCLUDE_FROM_ALL tests/manual/lobby_ui.cpp)
target_link_libraries(phoenix_lobby_smoke PRIVATE phoenix_network_lobby phoenix_presentation)
if(EMSCRIPTEN)
	set_target_properties(phoenix_lobby_smoke PROPERTIES SUFFIX ".html")
	target_link_options(
		phoenix_lobby_smoke
		PRIVATE
			"-O1"
			"-sEXPORTED_RUNTIME_METHODS=['ccall']"
			"-sALLOW_MEMORY_GROWTH=1"
			"-sSTACK_SIZE=1048576"
			"-sSTACK_OVERFLOW_CHECK=2"
			"--preload-file"
			"${CMAKE_CURRENT_BINARY_DIR}/assets@/assets"
			"SHELL:--preload-file \"${CMAKE_CURRENT_BINARY_DIR}/base.zip@/assets/base.zip\""
			"--shell-file"
			"${CMAKE_CURRENT_SOURCE_DIR}/tests/manual/lobby_ui.html"
	)
	set_property(
		TARGET phoenix_lobby_smoke
		APPEND
		PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/base.zip" "${CMAKE_CURRENT_SOURCE_DIR}/tests/manual/lobby_ui.html"
	)
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(
	NAME system.inference-adapters
	COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/system/inference_adapters.py
)
include(cmake/Modules.cmake)
include(cmake/ModuleChecks.cmake)
include(cmake/BuildPerformance.cmake)
foreach(
	frontend
	phoenix_session_api
	phoenix_application_shell
	phoenix_ui_navigation
	phoenix_console_model
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

# Manually invoked combat UI journey; deliberately excluded from CI/CTest.
if(NOT EMSCRIPTEN)
	add_executable(phoenix_combat_smoke EXCLUDE_FROM_ALL tests/manual/combat_ui.cpp)
	target_link_libraries(phoenix_combat_smoke PRIVATE phoenix_session phoenix_presentation)
	add_dependencies(phoenix_combat_smoke base_pack)
endif()

# Explicit graphical Comms evidence; not an automatic acceptance test.
if(NOT EMSCRIPTEN)
	add_executable(phoenix_boarding_smoke EXCLUDE_FROM_ALL tests/manual/boarding_ui.cpp)
	set_property(TARGET phoenix_boarding_smoke PROPERTY CXX_SCAN_FOR_MODULES ON)
	target_link_libraries(phoenix_boarding_smoke PRIVATE phoenix_session phoenix_presentation phoenix_application_shell)
	add_dependencies(phoenix_boarding_smoke base_pack)
	add_executable(phoenix_away_smoke EXCLUDE_FROM_ALL tests/manual/away_ui.cpp)
	set_property(TARGET phoenix_away_smoke PROPERTY CXX_SCAN_FOR_MODULES ON)
	add_executable(phoenix_ftl_smoke EXCLUDE_FROM_ALL tests/manual/ftl_ui.cpp)
	target_link_libraries(phoenix_ftl_smoke PRIVATE phoenix_session phoenix_presentation)
	add_dependencies(phoenix_ftl_smoke base_pack)
	target_link_libraries(
		phoenix_away_smoke
		PRIVATE phoenix_session phoenix_presentation phoenix_application_shell phoenix_console_model
	)
	add_dependencies(phoenix_away_smoke base_pack)
	add_executable(phoenix_comms_smoke EXCLUDE_FROM_ALL tests/manual/comms_ui.cpp)
	target_link_libraries(phoenix_comms_smoke PRIVATE phoenix_session phoenix_presentation)
	add_dependencies(phoenix_comms_smoke base_pack)
endif()

include(cmake/cruiser-smoke.cmake)

add_executable(phoenix_psychic_smoke EXCLUDE_FROM_ALL tests/manual/psychic_ui.cpp)
set_property(TARGET phoenix_psychic_smoke PROPERTY CXX_SCAN_FOR_MODULES ON)
target_link_libraries(phoenix_psychic_smoke PRIVATE phoenix_session phoenix_presentation phoenix_application_shell)
if(NOT EMSCRIPTEN)
	add_dependencies(phoenix_psychic_smoke base_pack)
endif()

include(cmake/runtime-profile.cmake)

# Existing synthetic cases import these narrow production contracts directly.
target_link_libraries(phoenix_simulation PRIVATE phoenix_gameplay_logistics)
target_link_libraries(phoenix_simulation PRIVATE phoenix_gameplay_sensor_envelopes phoenix_gameplay_psychic_state)
target_link_libraries(
	phoenix_simulation
	PRIVATE
		phoenix_gameplay_personnel_treatment
		phoenix_gameplay_psychic_contributions
		phoenix_gameplay_psychic_interference
		phoenix_gameplay_psychic_emission
)
target_link_libraries(
	phoenix_simulation
	PRIVATE phoenix_gameplay_away_state phoenix_world_persistence phoenix_control_templates
)
target_link_libraries(phoenix_presentation PRIVATE phoenix_ui_pad_dispatch)
target_link_libraries(phoenix_presentation PRIVATE phoenix_platform_viewport)
target_link_libraries(phoenix_simulation PRIVATE phoenix_gameplay_ftl_coordinator)
target_link_libraries(phoenix_simulation PRIVATE phoenix_gameplay_personnel_clinical phoenix_gameplay_personnel_runtime)
target_link_libraries(phoenix_simulation PRIVATE phoenix_scenarios_objectives)
target_link_libraries(phoenix_simulation PRIVATE phoenix_scenarios_streaming)
if(EMSCRIPTEN)
	target_link_libraries(phoenix_transfer_check PRIVATE phoenix_knowledge_read_models)
else()
	target_link_libraries(phoenix_transfer_tests PRIVATE phoenix_knowledge_read_models)
endif()
target_link_libraries(
	phoenix_simulation
	PRIVATE
		phoenix_effects_preparation
		phoenix_effects_funding
		phoenix_effects_bindings
		phoenix_effects_ability_validation
		phoenix_effects_continuation_validation
		phoenix_effects_clocks
		phoenix_gameplay_sensor_evidence
		phoenix_gameplay_sensor_coverage
		phoenix_gameplay_sensor_acquisition
		phoenix_gameplay_psychic_projection
		phoenix_gameplay_psychic_receptions
)
foreach(target phoenix_controller_runtime_tests)
	target_link_libraries(
		${target}
		PRIVATE phoenix_graphics_geometry phoenix_graphics_audio_policy phoenix_ui_engineering_widgets
	)
endforeach()
target_link_libraries(phoenix_combat_tests PRIVATE phoenix_graphics_audio_policy)
target_link_libraries(phoenix_away_tests PRIVATE phoenix_effects_contracts)
foreach(target phoenix_conversation_tests phoenix_personnel_tests phoenix_security_tests)
	target_link_libraries(${target} PRIVATE phoenix_knowledge_read_models)
endforeach()
if(EMSCRIPTEN)
	target_link_libraries(phoenix_input_check PRIVATE phoenix_knowledge_read_models)
	target_link_libraries(phoenix_simulation_check PRIVATE phoenix_effects_processes)
else()
	target_link_libraries(phoenix_menu_tests PRIVATE phoenix_graphics_geometry phoenix_ui_skin)
	target_link_libraries(phoenix_system_tests PRIVATE phoenix_knowledge_read_models)
	target_link_libraries(phoenix_simulation_tests PRIVATE phoenix_effects_processes)
endif()
foreach(
	target
	IN
	ITEMS phoenix_combat_tests phoenix_processes_tests phoenix_summary_bindings_tests phoenix_workshop_test_tests
)
	target_link_libraries(${target} PRIVATE phoenix_content_documents)
endforeach()
target_link_libraries(phoenix_rules_tests PRIVATE phoenix_effects_rules)
if(EMSCRIPTEN)
	target_link_libraries(phoenix_file_check PRIVATE phoenix_content_documents)
	target_link_libraries(phoenix_fleet_setup_check PRIVATE phoenix_content_documents)
	target_link_libraries(phoenix_input_check PRIVATE phoenix_content_compilers phoenix_content_packs)
else()
	target_link_libraries(phoenix_file_tests PRIVATE phoenix_content_documents)
	target_link_libraries(phoenix_fleet_setup_tests PRIVATE phoenix_content_documents)
	target_link_libraries(phoenix_menu_tests PRIVATE phoenix_content_documents)
	target_link_libraries(phoenix_system_tests PRIVATE phoenix_content_compilers phoenix_content_packs)
endif()
target_link_libraries(phoenix_gameplay_flight_tests PRIVATE phoenix_resources_equipment)
if(EMSCRIPTEN)
	target_link_libraries(phoenix_simulation_check PRIVATE phoenix_content_distribution)
else()
	target_link_libraries(phoenix_simulation_tests PRIVATE phoenix_content_distribution)
endif()
foreach(
	target
	IN
	ITEMS
		phoenix_abilities_tests
		phoenix_away_tests
		phoenix_camera_tests
		phoenix_cargo_tests
		phoenix_combat_tests
		phoenix_controller_runtime_tests
		phoenix_craft_tests
		phoenix_directed_tests
		phoenix_docking_tests
		phoenix_ftl_tests
		phoenix_medical_tests
		phoenix_missions_tests
		phoenix_processes_tests
		phoenix_psychic_tests
		phoenix_sensors_tests
)
	target_link_libraries(${target} PRIVATE phoenix_network_projection)
endforeach()
foreach(
	target
	IN
	ITEMS phoenix_controller_runtime_tests phoenix_docking_tests phoenix_medical_tests phoenix_sensors_tests
)
	target_link_libraries(${target} PRIVATE phoenix_knowledge_read_models)
endforeach()
foreach(
	target
	IN
	ITEMS
		phoenix_controller_runtime_tests
		phoenix_combat_tests
		phoenix_gameplay_flight_tests
		phoenix_sensors_tests
		phoenix_mission_smoke
		phoenix_lobby_smoke
)
	target_link_libraries(${target} PRIVATE phoenix_content_distribution)
endforeach()
if(EMSCRIPTEN)
	target_link_libraries(phoenix_transfer_check PRIVATE phoenix_network_projection)
else()
	target_link_libraries(phoenix_transfer_tests PRIVATE phoenix_network_projection)
	target_link_libraries(phoenix_menu_tests PRIVATE phoenix_content_distribution)
	target_link_libraries(phoenix_lobby_tests PRIVATE phoenix_content_distribution)
endif()

# Batch 2 typed owners; activate their adapters only after standalone compilation.
target_link_libraries(
	phoenix_simulation
	PRIVATE
		phoenix_world_assembly
		phoenix_control_protocol
		phoenix_control_coordination_facts
		phoenix_control_handover
		phoenix_control_views
		phoenix_gameplay_away_operations
		phoenix_gameplay_boarding_runtime
		phoenix_gameplay_away_crew
		phoenix_gameplay_directed
		phoenix_gameplay_ordnance
		phoenix_gameplay_combat_continuation
		phoenix_gameplay_weapons
		phoenix_resources_processing
		phoenix_gameplay_machinery
		phoenix_gameplay_flight_work
		phoenix_effects_machinery
		phoenix_effects_finite_work
		phoenix_effects_process_validation
)

target_link_libraries(
	phoenix_simulation
	PRIVATE phoenix_gameplay_sensor_findings phoenix_gameplay_sensor_validation phoenix_gameplay_sensor_projection
)

# Synthetic cases link only the presentation contracts they exercise.
target_link_libraries(phoenix_gameplay_flight_tests PRIVATE phoenix_console_model)
target_link_libraries(phoenix_sensors_tests PRIVATE phoenix_console_model)
target_link_libraries(phoenix_controller_runtime_tests PRIVATE phoenix_ui_navigation phoenix_console_model)

# These integration cases also call the Console free-function contracts.
target_link_libraries(phoenix_craft_tests PRIVATE phoenix_console_model)
target_link_libraries(phoenix_gameplay_knowledge_tests PRIVATE phoenix_console_model)

# Executable-level native and browser adapters own their implementation modules.
add_subdirectory(src/scenarios/interdictions)
add_subdirectory(src/world/schedule)

# Native World composition delegates state and policy to their family owners.
target_link_libraries(
	phoenix_simulation
	PRIVATE
		phoenix_effects_properties_runtime
		phoenix_effects_active_rules
		phoenix_scenario_interdictions
		phoenix_gameplay_flight_native
		phoenix_gameplay_away_runtime
		phoenix_world_profiling
)

if(EMSCRIPTEN)
	target_link_libraries(server PRIVATE phoenix_browser_frame_profile phoenix_workshop_browser_test_client)
	target_link_libraries(client PRIVATE phoenix_browser_frame_profile)
	target_link_libraries(workshop PRIVATE phoenix_browser_frame_profile phoenix_workshop_browser_test_client)
else()
	target_link_libraries(native PRIVATE phoenix_native_frame_profile phoenix_native_workshop_bridge)
endif()

# Explicit module ceilings and role guards validate the final configured graph.
include(cmake/ModulePolicy.cmake)
