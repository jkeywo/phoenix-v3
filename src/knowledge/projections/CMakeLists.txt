# Portable immutable decision contracts and presentation encoders. Native producers
# depend on these leaves; none links authoritative execution or Controller state.
foreach(
	domain
	IN
	ITEMS machinery combat personnel travel operations psychic
)
	add_library(phoenix_knowledge_decision_${domain} STATIC decision_${domain}.cpp)
	target_sources(phoenix_knowledge_decision_${domain} PUBLIC FILE_SET CXX_MODULES FILES decision_${domain}.cppm)
	target_link_libraries(phoenix_knowledge_decision_${domain} PUBLIC phoenix_contracts)
	set_property(TARGET phoenix_knowledge_decision_${domain} PROPERTY CXX_SCAN_FOR_MODULES ON)
endforeach()
target_sources(phoenix_knowledge_decision_travel PRIVATE decision_travel_decode.cpp)
target_sources(phoenix_knowledge_decision_combat PRIVATE decision_combat_decode.cpp)
target_link_libraries(phoenix_knowledge_decision_machinery PUBLIC phoenix_behavior_state_tree_definitions)
target_link_libraries(phoenix_knowledge_decision_combat PUBLIC phoenix_knowledge_records_definitions)
target_link_libraries(phoenix_knowledge_decision_psychic PRIVATE phoenix_knowledge_read_models)

add_library(phoenix_knowledge_decision STATIC)
target_sources(phoenix_knowledge_decision PUBLIC FILE_SET CXX_MODULES FILES decision.cppm)
target_link_libraries(
	phoenix_knowledge_decision
	PUBLIC
		phoenix_knowledge_decision_machinery
		phoenix_knowledge_decision_combat
		phoenix_knowledge_decision_personnel
		phoenix_knowledge_decision_travel
		phoenix_knowledge_decision_operations
		phoenix_knowledge_decision_psychic
)
set_property(TARGET phoenix_knowledge_decision PROPERTY CXX_SCAN_FOR_MODULES ON)

add_library(phoenix_knowledge_projections STATIC projections.cpp)
target_sources(phoenix_knowledge_projections PUBLIC FILE_SET CXX_MODULES FILES projections.cppm)
target_link_libraries(
	phoenix_knowledge_projections
	PUBLIC phoenix_control_controllers phoenix_knowledge_records_definitions phoenix_knowledge_decision
)
set_property(TARGET phoenix_knowledge_projections PROPERTY CXX_SCAN_FOR_MODULES ON)
