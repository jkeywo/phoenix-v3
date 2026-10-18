# Keep batching limited to pinned dependencies. First-party named modules retain
# separate translation units and CMake's module dependency scanning.
option(PHOENIX_FAST_DEPENDENCIES "Batch and parallelise compilation of pinned dependencies" ON)
if(NOT PHOENIX_FAST_DEPENDENCIES)
	return()
endif()

set_target_properties(rmlui_core flecs_static PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 8)
# TemplateCache and StyleSheetSpecification define different namespace-static
# `instance` objects. Keep TemplateCache separate without editing vendor code.
set_source_files_properties(
	"${rmlui_SOURCE_DIR}/Source/Core/TemplateCache.cpp"
	TARGET_DIRECTORY rmlui_core
	PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON
)

# MSBuild --parallel schedules projects, not individual sources within a project.
# Limit each of these large dependencies to four compiler processes; RmlUi has
# its own upstream /MP setting. Ninja already schedules individual compilations.
if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
	foreach(dependency flecs_static SDL3-static datachannel-static mbedcrypto)
		target_compile_options(${dependency} PRIVATE /MP4)
	endforeach()
endif()
