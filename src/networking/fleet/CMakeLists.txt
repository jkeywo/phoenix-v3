add_library(phoenix_fleet STATIC fleet.cpp)
target_sources(phoenix_fleet PUBLIC FILE_SET CXX_MODULES FILES fleet.cppm)
target_link_libraries(phoenix_fleet PUBLIC phoenix_persistence phoenix_wire phoenix_transport)
set_property(TARGET phoenix_fleet PROPERTY CXX_SCAN_FOR_MODULES ON)
set_property(
	TARGET phoenix_fleet
	PROPERTY PHOENIX_ALLOWED_DEPENDENCIES "phoenix_persistence;phoenix_wire;phoenix_transport"
)
set_property(GLOBAL APPEND PROPERTY PHOENIX_DEPENDENCY_MODULES phoenix_fleet)

# Server-only composition is a distinct target even though it shares this folder.
add_library(phoenix_fleet_setup STATIC fleet_setup.cpp fleet_runtime.cpp)
target_sources(phoenix_fleet_setup PUBLIC FILE_SET CXX_MODULES FILES fleet_setup.cppm)
target_link_libraries(phoenix_fleet_setup PUBLIC phoenix_session phoenix_wire phoenix_fleet phoenix_blobs)
set_property(TARGET phoenix_fleet_setup PROPERTY CXX_SCAN_FOR_MODULES ON)
set_property(
	TARGET phoenix_fleet_setup
	PROPERTY PHOENIX_ALLOWED_DEPENDENCIES "phoenix_session;phoenix_wire;phoenix_fleet;phoenix_blobs"
)
set_property(GLOBAL APPEND PROPERTY PHOENIX_DEPENDENCY_MODULES phoenix_fleet_setup)

target_link_libraries(phoenix_fleet_setup PRIVATE phoenix_content_compilers)
set_property(TARGET phoenix_fleet_setup APPEND PROPERTY PHOENIX_ALLOWED_DEPENDENCIES phoenix_content_compilers)
