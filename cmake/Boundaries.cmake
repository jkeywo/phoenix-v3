# Architecture checks inspect every configuration branch conservatively. A link
# hidden behind $<CONFIG:...>, LINK_ONLY, or BUILD_INTERFACE is still a dependency
# of this repository, even when the currently selected configuration omits it.
function(phoenix_link_targets target result)
	set(dependencies "")
	# These operators select literal branches or decorate a named link target.
	# Computed names/property evaluation need a real evaluator; refuse them rather
	# than silently approving an incomplete graph from their spelling.
	set(
		branch_operators
		AND
		OR
		NOT
		BOOL
		IF
		CONFIG
		STREQUAL
		EQUAL
		VERSION_LESS
		VERSION_GREATER
		VERSION_EQUAL
		VERSION_LESS_EQUAL
		VERSION_GREATER_EQUAL
		PLATFORM_ID
		C_COMPILER_ID
		CXX_COMPILER_ID
		LINK_LANGUAGE
		LINK_LANG_AND_ID
		TARGET_EXISTS
		TARGET_NAME_IF_EXISTS
		BUILD_INTERFACE
		BUILD_LOCAL_INTERFACE
		INSTALL_INTERFACE
		LINK_ONLY
		COMPILE_ONLY
		LINK_LIBRARY
		LINK_GROUP
		HOST_LINK
		DEVICE_LINK
	)
	if(NOT TARGET "${target}")
		set(${result} "" PARENT_SCOPE)
		return()
	endif()
	get_target_property(aliased "${target}" ALIASED_TARGET)
	if(aliased)
		set(target "${aliased}")
	endif()
	get_property(has_policy TARGET "${target}" PROPERTY PHOENIX_ALLOWED_DEPENDENCIES SET)
	foreach(property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES INTERFACE_LINK_LIBRARIES_DIRECT)
		get_target_property(links "${target}" "${property}")
		if(links)
			set(candidates "")
			foreach(link IN LISTS links)
				if(TARGET "${link}" OR link MATCHES "^phoenix_[A-Za-z0-9_.+-]+$")
					list(APPEND candidates "${link}")
				elseif(link MATCHES "\\$<")
					if(has_policy)
						if(NOT link MATCHES "^\\$<" OR link MATCHES "[A-Za-z0-9_.+-]\\$<|>[A-Za-z0-9_.+-]|>\\$<|>::|::\\$<")
							message(FATAL_ERROR "Unsupported computed link dependency: ${target} -> ${link}")
						endif()
						string(REGEX MATCHALL "\\$<[A-Za-z_][A-Za-z0-9_]*" operators "${link}")
						foreach(expression IN LISTS operators)
							string(SUBSTRING "${expression}" 2 -1 operator)
							if(NOT operator IN_LIST branch_operators)
								message(FATAL_ERROR "Unsupported computed link dependency: ${target} -> ${link} (${operator})")
							endif()
						endforeach()
					endif()
					# Single ':' separates operators; '::' remains part of an
					# imported target name. Ordinary flags/file paths are ignored.
					string(REGEX MATCHALL "[A-Za-z_][A-Za-z0-9_.+-]*(::[A-Za-z_][A-Za-z0-9_.+-]*)*" branch_targets "${link}")
					list(APPEND candidates ${branch_targets})
				endif()
			endforeach()
			foreach(candidate IN LISTS candidates)
				if(TARGET "${candidate}")
					get_target_property(candidate_alias "${candidate}" ALIASED_TARGET)
					if(candidate_alias)
						list(APPEND dependencies "${candidate_alias}")
					else()
						list(APPEND dependencies "${candidate}")
					endif()
				elseif(candidate MATCHES "^phoenix_[A-Za-z0-9_.+-]+$")
					# A misspelled first-party dependency is still checked against
					# policy rather than disappearing as an ordinary -l argument.
					list(APPEND dependencies "${candidate}")
				endif()
			endforeach()
		endif()
	endforeach()
	list(REMOVE_DUPLICATES dependencies)
	set(${result} "${dependencies}" PARENT_SCOPE)
endfunction()

# Each role refusal visits each reachable target once for the entire forbidden
# set. Reset between calls so late mutations are never hidden by previous checks.
function(phoenix_forbid_dependency_visit target forbidden trace)
	if(TARGET "${target}")
		get_target_property(target_alias "${target}" ALIASED_TARGET)
		if(target_alias)
			set(target "${target_alias}")
		endif()
	endif()
	if("${target}" IN_LIST forbidden)
		message(FATAL_ERROR "Forbidden link dependency: ${target} (via ${trace})")
	endif()
	get_property(visited GLOBAL PROPERTY PHOENIX_BOUNDARY_VISITED)
	if(NOT TARGET "${target}" OR "${target}" IN_LIST visited)
		return()
	endif()
	set_property(GLOBAL APPEND PROPERTY PHOENIX_BOUNDARY_VISITED "${target}")
	list(APPEND trace "${target}")
	phoenix_link_targets("${target}" dependencies)
	foreach(dependency IN LISTS dependencies)
		phoenix_forbid_dependency_visit("${dependency}" "${forbidden}" "${trace}")
	endforeach()
endfunction()

function(phoenix_forbid_dependencies target forbidden)
	set(resolved "")
	foreach(candidate IN LISTS forbidden)
		if(TARGET "${candidate}")
			get_target_property(aliased "${candidate}" ALIASED_TARGET)
			if(aliased)
				set(candidate "${aliased}")
			endif()
		endif()
		list(APPEND resolved "${candidate}")
	endforeach()
	list(REMOVE_DUPLICATES resolved)
	set_property(GLOBAL PROPERTY PHOENIX_BOUNDARY_VISITED "")
	phoenix_forbid_dependency_visit("${target}" "${resolved}" "${ARGN}")
endfunction()

# Keep the historical single-target caller contract, including a trace prefix.
function(phoenix_forbid_dependency target forbidden visited)
	phoenix_forbid_dependencies("${target}" "${forbidden}" "${visited}")
endfunction()

# Register after creating a module target. Validation is deliberately separate so
# all target/alias names exist before generator-expression references are read.
function(phoenix_module_dependencies target)
	if(NOT TARGET "${target}")
		message(FATAL_ERROR "Cannot register dependencies of missing module target: ${target}")
	endif()
	cmake_parse_arguments(policy "" "" "ALLOW" ${ARGN})
	if(policy_UNPARSED_ARGUMENTS)
		message(FATAL_ERROR "Unexpected module dependency arguments for ${target}: ${policy_UNPARSED_ARGUMENTS}")
	endif()
	set_property(TARGET "${target}" PROPERTY PHOENIX_ALLOWED_DEPENDENCIES "${policy_ALLOW}")
	set_property(GLOBAL APPEND PROPERTY PHOENIX_DEPENDENCY_MODULES "${target}")
endfunction()

function(phoenix_validate_module_dependencies)
	get_property(modules GLOBAL PROPERTY PHOENIX_DEPENDENCY_MODULES)
	list(REMOVE_DUPLICATES modules)
	foreach(target IN LISTS modules)
		get_target_property(allowed "${target}" PHOENIX_ALLOWED_DEPENDENCIES)
		set(resolved_allowed "")
		foreach(dependency IN LISTS allowed)
			if(NOT TARGET "${dependency}")
				if(dependency MATCHES "^phoenix_" OR dependency MATCHES "::")
					message(FATAL_ERROR "Allowed dependency of ${target} is not a target: ${dependency}")
				endif()
				# Plain names may be platform system libraries (ole32, user32,
				# pthread). Target-edge extraction also ignores non-targets.
				continue()
			endif()
			get_target_property(aliased "${dependency}" ALIASED_TARGET)
			if(aliased)
				list(APPEND resolved_allowed "${aliased}")
			else()
				list(APPEND resolved_allowed "${dependency}")
			endif()
		endforeach()
		phoenix_link_targets("${target}" actual)
		foreach(dependency IN LISTS actual)
			if(NOT dependency IN_LIST resolved_allowed)
				message(
					FATAL_ERROR
					"Undeclared module dependency: ${target} -> ${dependency}; add an intentional allowed dependency or remove the link"
				)
			endif()
		endforeach()
	endforeach()
endfunction()

# Refuse unregistered first-party libraries as well as forbidden links. The
# coverage inventory reads target identity/source ownership, never derives an
# allowed set from the graph being checked. Test/executable targets and vendored
# library implementations are outside the module-policy inventory.
function(phoenix_require_module_policies directory)
	get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
	foreach(target IN LISTS targets)
		if(NOT target MATCHES "^phoenix_")
			continue()
		endif()
		get_target_property(kind "${target}" TYPE)
		if(NOT kind MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|INTERFACE_LIBRARY|OBJECT_LIBRARY|MODULE_LIBRARY)$")
			continue()
		endif()
		string(FIND "${directory}/" "${PROJECT_SOURCE_DIR}/src/" source_prefix)
		set(first_party FALSE)
		if(source_prefix EQUAL 0)
			set(first_party TRUE)
		else()
			get_target_property(sources "${target}" SOURCES)
			foreach(source IN LISTS sources)
				if(NOT IS_ABSOLUTE "${source}")
					set(source "${directory}/${source}")
				endif()
				string(FIND "${source}" "${PROJECT_SOURCE_DIR}/src/" source_prefix)
				if(source_prefix EQUAL 0)
					set(first_party TRUE)
				endif()
			endforeach()
		endif()
		if(first_party)
			get_property(has_policy TARGET "${target}" PROPERTY PHOENIX_ALLOWED_DEPENDENCIES SET)
			if(NOT has_policy)
				message(FATAL_ERROR "Module target has no intentional dependency policy: ${target}")
			endif()
			# Property-only registrations are also checked; forgetting to append
			# to the registry cannot bypass direct-set validation.
			set_property(GLOBAL APPEND PROPERTY PHOENIX_DEPENDENCY_MODULES "${target}")
		endif()
	endforeach()
	get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
	foreach(child IN LISTS children)
		string(FIND "${child}/" "${PROJECT_SOURCE_DIR}/src/" source_prefix)
		if(source_prefix EQUAL 0)
			phoenix_require_module_policies("${child}")
		endif()
	endforeach()
endfunction()
