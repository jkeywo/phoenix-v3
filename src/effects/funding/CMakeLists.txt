add_library(phoenix_effects_funding STATIC funding.cpp)
target_sources(phoenix_effects_funding PUBLIC FILE_SET CXX_MODULES FILES funding.cppm)
target_link_libraries(
	phoenix_effects_funding
	PUBLIC phoenix_effects_transactions phoenix_effects_native
	PRIVATE flecs_static
)
