add_library(phoenix_control_objectives STATIC objectives.cpp)
target_sources(phoenix_control_objectives PUBLIC FILE_SET CXX_MODULES FILES objectives.cppm)
set_property(TARGET phoenix_control_objectives PROPERTY CXX_SCAN_FOR_MODULES ON)
add_library(
	phoenix_control_decisions
	STATIC
	decisions.cpp
	report_facts.cpp
	inspection_evidence.cpp
	contact_candidates.cpp
	armament_candidates.cpp
	scan_candidates.cpp
	evidence_candidates.cpp
	delivery_candidates.cpp
	personnel_candidates.cpp
	docking_candidates.cpp
	workflow_facts.cpp
	medical_facts.cpp
	repair_facts.cpp
	observations.cpp
	objective_choices.cpp
	candidate_adapters.cpp
)
target_sources(phoenix_control_decisions PUBLIC FILE_SET CXX_MODULES FILES decisions.cppm)
target_link_libraries(
	phoenix_control_decisions
	PUBLIC
		phoenix_control_objectives
		phoenix_control_controllers
		phoenix_world_state
		phoenix_knowledge_projections
		phoenix_content_effects_types
	PRIVATE
		phoenix_control_coordination
		phoenix_effects_contracts
		phoenix_effects_processes
		phoenix_gameplay_flight
		phoenix_gameplay_ordnance_contacts
		phoenix_behavior_state_tree
)
set_property(TARGET phoenix_control_decisions PROPERTY CXX_SCAN_FOR_MODULES ON)
