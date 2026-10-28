# Portable preferences are registered early beside the UI contracts, before this
# application composition family is registered from the root.
add_subdirectory(library)
add_subdirectory(shell)
add_subdirectory(preference-store)
add_subdirectory(displays)
add_subdirectory(connections)

foreach(
	target
	IN
	ITEMS
		phoenix_application_library
		phoenix_application_shell
		phoenix_application_preference_store
		phoenix_application_displays
		phoenix_application_connections
)
	set_target_properties(
		${target}
		PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_SCAN_FOR_MODULES ON
	)
endforeach()
