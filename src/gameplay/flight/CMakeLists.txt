include(definitions.cmake)

add_library(phoenix_gameplay_flight STATIC flight.cpp basic.cpp)
target_sources(phoenix_gameplay_flight PUBLIC FILE_SET CXX_MODULES FILES runtime.cppm)
target_link_libraries(
	phoenix_gameplay_flight
	PUBLIC phoenix_gameplay_flight_definitions phoenix_world_state
	PRIVATE phoenix_resources_stocks_definitions
)
set_property(TARGET phoenix_gameplay_flight PROPERTY CXX_SCAN_FOR_MODULES ON)
