# Source ownership is checked during ordinary builds as well as CTest. Exact
# transitional exceptions expire as soon as the corresponding edge disappears.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_target(
	phoenix_module_boundaries
	ALL
	COMMAND
		"${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/check-module-boundaries.py" --root "${PROJECT_SOURCE_DIR}"
		--exceptions "${PROJECT_SOURCE_DIR}/scripts/module-boundary-exceptions.json"
	COMMENT "Checking module ownership and private implementation boundaries"
	VERBATIM
)
add_test(
	NAME architecture.module-boundaries
	COMMAND
		"${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/check-module-boundaries.py" --root "${PROJECT_SOURCE_DIR}"
		--exceptions "${PROJECT_SOURCE_DIR}/scripts/module-boundary-exceptions.json"
)
add_test(
	NAME architecture.boundary-fixtures
	COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/system/module_boundaries.py" --cmake "${CMAKE_COMMAND}"
)
