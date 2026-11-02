# Features are independent. Distribution presets only choose their values.
option(PHOENIX_ENABLE_WORKSHOP "Build Workshop authoring tools" ON)
option(PHOENIX_ENABLE_GM "Enable GM hosting and inspection" ON)
option(PHOENIX_ENABLE_MULTISHIP "Allow additional player ship hosts" ON)
option(PHOENIX_ENABLE_MODS "Allow user content packs" ON)
option(PHOENIX_BUNDLE_TEST_MISSIONS "Bundle the Test Missions child pack" ON)
set(PHOENIX_STORAGE_NAMESPACE "" CACHE STRING "Distribution-specific storage suffix")
set(
	PHOENIX_PACK_INPUT_DIRECTORY
	"${PROJECT_SOURCE_DIR}/build/native"
	CACHE PATH
	"Verified native packs for browser builds"
)
if(PHOENIX_BUNDLE_TEST_MISSIONS AND NOT PHOENIX_ENABLE_MODS)
	message(FATAL_ERROR "Test Missions is a child mod: enable mods or disable PHOENIX_BUNDLE_TEST_MISSIONS")
endif()
if(NOT PHOENIX_STORAGE_NAMESPACE MATCHES "^[a-z0-9-]*$")
	message(FATAL_ERROR "PHOENIX_STORAGE_NAMESPACE must contain only lowercase letters, digits and hyphens")
endif()
foreach(
	feature
	ENABLE_WORKSHOP
	ENABLE_GM
	ENABLE_MULTISHIP
	ENABLE_MODS
	BUNDLE_TEST_MISSIONS
)
	if(PHOENIX_${feature})
		set(PHOENIX_${feature}_JSON true)
		add_compile_definitions(PHOENIX_${feature}=1)
	else()
		set(PHOENIX_${feature}_JSON false)
		add_compile_definitions(PHOENIX_${feature}=0)
	endif()
endforeach()
add_compile_definitions(PHOENIX_STORAGE_NAMESPACE="${PHOENIX_STORAGE_NAMESPACE}")
