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

export module phoenix.content.operations;
export import phoenix.data;
export import phoenix.state_tree;
export import phoenix.content.catalog;
export import phoenix.content.combat.types;
export import phoenix.content.effects.types;
export import phoenix.content.operations.types;

export namespace phoenix
{
// Authored personnel is compiled once; runtime assignments never alter these records.
state_tree::Registry team_registry();
std::vector<BoardingDefinition> compile_boarding(const Catalog &, const std::string &entity);
std::vector<BoardingDefinition> compile_boarding(const Catalog &, const std::string &entity, const DirectedCatalog &,
                                                 SharedPropertyCatalog = {});
state_tree::Registry away_registry(const Json &tree = Json::object());
std::vector<AwayDefinition> compile_away(const Catalog &, const Json &);
std::vector<AwayDefinition> compile_away(const Catalog &, const Json &, const DirectedCatalog &,
                                         SharedPropertyCatalog = {});
PersonnelDefinition compile_personnel(const Catalog &, const std::string &entity, SharedPropertyCatalog = {});
PersonnelDefinition compile_personnel(const Catalog &, const std::string &entity, const DirectedCatalog &,
                                      SharedPropertyCatalog = {});
PersonnelDefinition compile_personnel(const Catalog &, const std::string &, const DirectedCatalog &,
                                      const RulesCatalog &, SharedPropertyCatalog = {});
} // namespace phoenix
