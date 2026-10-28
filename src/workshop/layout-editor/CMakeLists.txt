add_library(phoenix_workshop_layout STATIC layout.cpp)
target_sources(phoenix_workshop_layout PUBLIC FILE_SET CXX_MODULES FILES layout.cppm)
target_link_libraries(
	phoenix_workshop_layout
	PUBLIC phoenix_files phoenix_workshop_shell
	PRIVATE phoenix_ui_preview phoenix_imgui raylib
)

target_link_libraries(
	phoenix_workshop_layout
	PUBLIC phoenix_content_documents
	PRIVATE phoenix_content_packs phoenix_content_ui
)
