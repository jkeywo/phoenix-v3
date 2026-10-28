add_subdirectory(data)
add_subdirectory(contracts)
add_subdirectory(input)
add_subdirectory(console-controls)

# Transitional convenience target; declaration ownership belongs to the leaves.
add_library(phoenix_foundation INTERFACE)
target_link_libraries(
	phoenix_foundation
	INTERFACE phoenix_data phoenix_contracts phoenix_input phoenix_console_controls
)
