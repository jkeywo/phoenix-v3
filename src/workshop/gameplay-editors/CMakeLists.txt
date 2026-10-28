add_library(
	phoenix_workshop_gameplay
	STATIC
	gameplay_editor.cpp
	equipment.cpp
	combat.cpp
	operations.cpp
	resources.cpp
	effects.cpp
	knowledge.cpp
	scenarios.cpp
	consoles.cpp
)
target_sources(phoenix_workshop_gameplay PUBLIC FILE_SET CXX_MODULES FILES gameplay.cppm)
target_link_libraries(
	phoenix_workshop_gameplay
	PUBLIC phoenix_workshop_shell phoenix_workshop_test_client
	PRIVATE phoenix_workshop_tree phoenix_imgui
)

target_link_libraries(
	phoenix_workshop_gameplay
	PUBLIC phoenix_content_documents
	PRIVATE phoenix_content_catalog phoenix_content_compilers phoenix_content_effects_types
)
