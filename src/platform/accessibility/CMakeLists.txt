add_library(phoenix_accessibility STATIC)
target_sources(phoenix_accessibility PUBLIC FILE_SET CXX_MODULES FILES accessibility.cppm)
target_link_libraries(phoenix_accessibility PUBLIC phoenix_ui_interaction PRIVATE RmlUi::RmlUi)
if(EMSCRIPTEN)
	target_sources(phoenix_accessibility PRIVATE browser_semantics.cpp)
elseif(WIN32)
	target_sources(phoenix_accessibility PRIVATE windows_semantics.cpp)
	target_link_libraries(
		phoenix_accessibility
		PRIVATE uiautomationcore ole32 oleaut32 comctl32 imm32
	)
else()
	target_sources(phoenix_accessibility PRIVATE linux_semantics.cpp)
endif()
