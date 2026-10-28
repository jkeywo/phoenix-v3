add_library(
	phoenix_knowledge_read_models
	STATIC
	body.cpp
	property.cpp
	conversation.cpp
	display.cpp
	ability.cpp
	process.cpp
	rules.cpp
	personnel.cpp
	psychic.cpp
	away.cpp
	team.cpp
	craft.cpp
	logistics.cpp
	docking.cpp
	sensor.cpp
	directed.cpp
	ftl.cpp
	objective.cpp
)
target_sources(phoenix_knowledge_read_models PUBLIC FILE_SET CXX_MODULES FILES read_models.cppm)
target_link_libraries(phoenix_knowledge_read_models PUBLIC phoenix_foundation)
set_property(TARGET phoenix_knowledge_read_models PROPERTY CXX_SCAN_FOR_MODULES ON)
