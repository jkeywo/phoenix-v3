option(
	PHOENIX_WEB_DEVELOPMENT_LINK
	"Link web executables at -O0 for local development; retain compile optimisation"
	OFF
)
if(NOT PHOENIX_WEB_DEVELOPMENT_LINK)
	return()
endif()
if(NOT EMSCRIPTEN)
	message(FATAL_ERROR "PHOENIX_WEB_DEVELOPMENT_LINK is only supported by the Emscripten build")
endif()

# This is deliberately executable-only: libraries and module artifacts keep their
# configured optimisation. Append last so manual hosts' -O1/-O2 flags cannot win.
function(phoenix_development_link_directory directory)
	get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
	foreach(target IN LISTS targets)
		get_target_property(kind ${target} TYPE)
		if(kind STREQUAL "EXECUTABLE" AND target MATCHES "^(phoenix_.*|server|client|workshop)$")
			target_link_options(${target} PRIVATE -O0)
		endif()
	endforeach()
	get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
	foreach(child IN LISTS children)
		string(FIND "${child}/" "${PROJECT_SOURCE_DIR}/src/" first_party_prefix)
		if(first_party_prefix EQUAL 0)
			phoenix_development_link_directory("${child}")
		endif()
	endforeach()
endfunction()
phoenix_development_link_directory("${PROJECT_SOURCE_DIR}")
message(STATUS "Development web linking enabled (-O0); C++ compilation optimisation is unchanged")
