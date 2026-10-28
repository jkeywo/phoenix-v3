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

export module phoenix.content.scenarios;
export import phoenix.data;
export import phoenix.content.catalog;
export import phoenix.content.combat.types;
export import phoenix.content.effects.types;

export namespace phoenix
{
void verify_conversation_authoring(const Catalog &catalog);
void verify_scenario_authoring(const Catalog &catalog, const Json &map);
void verify_scenario_authoring(const Catalog &catalog, const Json &map, const DirectedCatalog &,
                               SharedPropertyCatalog = {});
// Resolved references, including future spawn content; no production-content assumptions.
std::vector<std::string> scenario_definitions(const Catalog &catalog, const std::string &map);
std::vector<std::string> scenario_assets(const Catalog &catalog, const std::string &map);
} // namespace phoenix
