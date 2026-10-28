# Portable preferences and application composition share one family owner.
# Cross-family target links are resolved after all families are registered.
add_subdirectory(preferences)
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
