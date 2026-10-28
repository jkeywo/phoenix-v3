add_library(phoenix_gameplay_machinery STATIC machinery.cpp)
target_sources(phoenix_gameplay_machinery PUBLIC FILE_SET CXX_MODULES FILES machinery.cppm)
target_link_libraries(
	phoenix_gameplay_machinery
	PUBLIC phoenix_effects_processes phoenix_content_operations_types
	PRIVATE
		phoenix_gameplay_fitting
		phoenix_gameplay_combat_faults
		phoenix_gameplay_ftl
		phoenix_resources_processing
		phoenix_resources_networks
		flecs_static
)
