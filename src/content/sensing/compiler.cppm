module;
#include <set>
#include <cstdint>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

export module phoenix.content.sensing;
export import phoenix.data;
export import phoenix.state_tree;
export import phoenix.content.catalog;
export import phoenix.content.combat.types;
export import phoenix.content.controllers.types;
export import phoenix.content.effects.types;
export import phoenix.content.operations.types;
export import phoenix.content.sensing.types;

export namespace phoenix
{
std::map<std::string, SensorDefinition> compile_sensors(const Catalog &, const std::string &);
std::map<std::string, SensorDefinition> compile_sensors(const Catalog &, const std::string &, const DirectedCatalog &);
std::vector<SensorFinding> compile_sensor_findings(const Catalog &, const Json &);
state_tree::Registry interdiction_registry();
std::vector<InterdictionDefinition> compile_interdictions(const Catalog &, const Json &);
std::vector<InterdictionDefinition> compile_interdictions(const Catalog &, const Json &, const DirectedCatalog &,
                                                          SharedPropertyCatalog = {});
PsychicDefinition compile_psychic(const Catalog &, const std::string &);
PsychicDefinition compile_psychic(const Catalog &, const std::string &, const DirectedCatalog &,
                                  SharedPropertyCatalog = {});
PsychicDefinition compile_psychic(const Catalog &, const std::string &, SharedAbilityCatalog,
                                  const PersonnelDefinition &, const ControllableLayout &);
PsychicPowerDefinition compile_psychic_effect(const Catalog &, const std::string &);
} // namespace phoenix
