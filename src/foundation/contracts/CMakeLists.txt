add_library(phoenix_contracts STATIC contracts.cpp serialization.cpp)
target_sources(phoenix_contracts PUBLIC FILE_SET CXX_MODULES FILES contracts.cppm)
target_link_libraries(phoenix_contracts PUBLIC phoenix_data)
set_property(TARGET phoenix_contracts PROPERTY CXX_SCAN_FOR_MODULES ON)
