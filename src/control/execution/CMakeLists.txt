add_library(
	phoenix_control_execution
	STATIC
	execution.cpp
	persistence.cpp
	decisions.cpp
	observations.cpp
	admission.cpp
)
target_sources(phoenix_control_execution PUBLIC FILE_SET CXX_MODULES FILES execution.cppm)
target_link_libraries(
	phoenix_control_execution
	PUBLIC phoenix_control_controllers phoenix_world_state phoenix_knowledge_projections phoenix_content_effects_types
	PRIVATE
		phoenix_control_coordination
		phoenix_effects_contracts
		phoenix_effects_processes
		phoenix_gameplay_flight
		phoenix_behavior_state_tree
)
set_property(TARGET phoenix_control_execution PROPERTY CXX_SCAN_FOR_MODULES ON)
