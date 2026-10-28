add_library(phoenix_effects_rules STATIC rules.cpp)
target_sources(phoenix_effects_rules PUBLIC FILE_SET CXX_MODULES FILES rules.cppm)
target_link_libraries(phoenix_effects_rules PUBLIC phoenix_content_effects_types)

# Mutable native clocks/query targets are authoritative; the evaluator above is definition-only.
add_library(phoenix_effects_rule_state STATIC state.cpp)
target_sources(phoenix_effects_rule_state PUBLIC FILE_SET CXX_MODULES FILES state.cppm)
target_link_libraries(phoenix_effects_rule_state PUBLIC phoenix_effects_contracts PRIVATE flecs_static)

add_library(phoenix_effects_bindings STATIC bindings.cpp)
target_sources(phoenix_effects_bindings PUBLIC FILE_SET CXX_MODULES FILES bindings.cppm)
target_link_libraries(phoenix_effects_bindings PUBLIC phoenix_effects_contracts phoenix_effects_rules)
