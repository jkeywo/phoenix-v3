add_library(
	phoenix_workshop_authoring_core
	STATIC
	core.cpp
	operations.cpp
	fields.cpp
	structure.cpp
	records.cpp
	ship.cpp
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fields.json")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/fields.json" PHOENIX_WORKSHOP_FIELDS HEX)
string(REGEX REPLACE "(..)" "0x\\1," PHOENIX_WORKSHOP_FIELDS "${PHOENIX_WORKSHOP_FIELDS}")
configure_file(fields_data.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/phoenix_workshop_fields.hpp" @ONLY)
target_include_directories(phoenix_workshop_authoring_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_sources(phoenix_workshop_authoring_core PUBLIC FILE_SET CXX_MODULES FILES core.cppm)
set_target_properties(
	phoenix_workshop_authoring_core
	PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_SCAN_FOR_MODULES ON
)
target_link_libraries(
	phoenix_workshop_authoring_core
	PUBLIC phoenix_content_documents
	PRIVATE phoenix_content_ui phoenix_content_assets
)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(
	NAME architecture.workshop-authoring
	COMMAND ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tests/system/workshop_authoring_architecture.py"
)
cmake_language(
	DEFER DIRECTORY "${PROJECT_SOURCE_DIR}"
	CALL phoenix_forbid_dependency
	phoenix_workshop_authoring_core
	raylib
	""
)
cmake_language(
	DEFER DIRECTORY "${PROJECT_SOURCE_DIR}"
	CALL phoenix_forbid_dependency
	phoenix_workshop_authoring_core
	phoenix_imgui
	""
)
cmake_language(
	DEFER DIRECTORY "${PROJECT_SOURCE_DIR}"
	CALL phoenix_forbid_dependency
	phoenix_workshop_authoring_core
	phoenix_simulation
	""
)
cmake_language(
	DEFER DIRECTORY "${PROJECT_SOURCE_DIR}"
	CALL phoenix_forbid_dependency
	phoenix_workshop_authoring_core
	flecs_static
	""
)

if(NOT EMSCRIPTEN)
	target_sources(phoenix_workshop_authoring_core PRIVATE filesystem.cpp)
	add_executable(phoenix_workshop ../../executables/workshop-cli/main.cpp)
	target_link_libraries(phoenix_workshop PRIVATE phoenix_workshop_authoring_core)
	add_executable(phoenix_workshop_cli_tests ../../../tests/system/workshop_authoring.cpp)
	target_link_libraries(phoenix_workshop_cli_tests PRIVATE phoenix_workshop_authoring_core)
	set_target_properties(phoenix_workshop phoenix_workshop_cli_tests PROPERTIES CXX_SCAN_FOR_MODULES ON)
	target_compile_definitions(
		phoenix_workshop_cli_tests
		PRIVATE PHOENIX_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures/content"
	)
	set_target_properties(
		phoenix_workshop
		phoenix_workshop_cli_tests
		PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
	)
	add_test(NAME system.workshop-authoring COMMAND phoenix_workshop_cli_tests)
	add_test(
		NAME system.workshop-cli
		COMMAND
			${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tests/system/workshop_cli_processes.py" $<TARGET_FILE:phoenix_workshop>
	)
endif()
