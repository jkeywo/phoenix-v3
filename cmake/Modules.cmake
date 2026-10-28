# Public interfaces follow the ownership of the existing static libraries.

target_sources(phoenix_simulation PUBLIC FILE_SET CXX_MODULES FILES src/world/lifecycle/simulation.cppm)

target_sources(phoenix_presentation PUBLIC FILE_SET CXX_MODULES FILES src/ui/runtime/presentation.cppm)

# Scan first-party implementation units as well as public interfaces. Family
# targets live in child directories; vendored dependency directories are never
# traversed and retain their own compilation policy.
function(phoenix_scan_module_directory directory)
	get_property(phoenix_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
	foreach(target IN LISTS phoenix_targets)
		if(
			target MATCHES "^phoenix_"
			AND NOT target MATCHES "^phoenix_(qr|zip|imgui|shaping)$"
			OR target MATCHES "^(native|server|client|workshop)$"
		)
			get_target_property(kind ${target} TYPE)
			if(NOT kind MATCHES "^(UTILITY|INTERFACE_LIBRARY)$")
				set_property(TARGET ${target} PROPERTY CXX_SCAN_FOR_MODULES ON)
			endif()
		endif()
	endforeach()
	get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
	foreach(child IN LISTS children)
		string(FIND "${child}/" "${PROJECT_SOURCE_DIR}/src/" first_party_prefix)
		if(first_party_prefix EQUAL 0)
			phoenix_scan_module_directory("${child}")
		endif()
	endforeach()
endfunction()
phoenix_scan_module_directory("${PROJECT_SOURCE_DIR}")
