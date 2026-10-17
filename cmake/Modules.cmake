# Public interfaces follow the ownership of the existing static libraries.
target_sources(phoenix_content PUBLIC FILE_SET CXX_MODULES FILES src/content/content.cppm)
target_sources(phoenix_simulation PUBLIC FILE_SET CXX_MODULES FILES src/simulation/simulation.cppm)
target_sources(phoenix_transport PUBLIC FILE_SET CXX_MODULES FILES src/transport/transport.cppm)
target_sources(phoenix_wire PUBLIC FILE_SET CXX_MODULES FILES src/network/wire.cppm)
target_sources(phoenix_blobs PUBLIC FILE_SET CXX_MODULES FILES src/network/blobs.cppm)
target_sources(phoenix_replication PUBLIC FILE_SET CXX_MODULES FILES src/replication/replication.cppm)
target_sources(phoenix_persistence PUBLIC FILE_SET CXX_MODULES FILES src/replication/persistence.cppm)
target_sources(phoenix_fleet PUBLIC FILE_SET CXX_MODULES FILES src/replication/fleet.cppm)
target_sources(phoenix_session_api PUBLIC FILE_SET CXX_MODULES FILES src/session/api.cppm)
target_sources(phoenix_session PUBLIC FILE_SET CXX_MODULES FILES src/session/local.cppm)
target_sources(phoenix_session PUBLIC FILE_SET CXX_MODULES FILES src/session/execution.cppm)
target_sources(phoenix_network_host PUBLIC FILE_SET CXX_MODULES FILES src/network/host.cppm)
target_sources(phoenix_network_client PUBLIC FILE_SET CXX_MODULES FILES src/network/client.cppm)
target_sources(phoenix_fleet_setup PUBLIC FILE_SET CXX_MODULES FILES src/session/fleet_setup.cppm)
target_sources(phoenix_core PUBLIC FILE_SET CXX_MODULES FILES src/core/application.cppm)
target_sources(phoenix_core PUBLIC FILE_SET CXX_MODULES FILES src/core/navigation.cppm)
target_sources(phoenix_core PUBLIC FILE_SET CXX_MODULES FILES src/core/console.cppm)
target_sources(phoenix_presentation PUBLIC FILE_SET CXX_MODULES FILES src/presentation/presentation.cppm)
target_sources(phoenix_tools PUBLIC FILE_SET CXX_MODULES FILES src/tools/tools.cppm)
target_sources(phoenix_files PUBLIC FILE_SET CXX_MODULES FILES src/platform/files.cppm)
target_sources(phoenix_devices PUBLIC FILE_SET CXX_MODULES FILES src/platform/devices.cppm)

# Scan first-party sources and consumers, never the vendored libraries.
get_property(phoenix_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(target IN LISTS phoenix_targets)
	if(
		target MATCHES "^phoenix_"
		AND NOT target MATCHES "^phoenix_(qr|zip|imgui)$"
		OR target MATCHES "^(native|server|client|workshop)$"
	)
		get_target_property(kind ${target} TYPE)
		if(NOT kind STREQUAL "UTILITY")
			set_property(TARGET ${target} PROPERTY CXX_SCAN_FOR_MODULES ON)
		endif()
	endif()
endforeach()
