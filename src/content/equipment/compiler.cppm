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

export module phoenix.content.equipment;
export import phoenix.data;
export import phoenix.content.catalog;
export import phoenix.content.combat.types;
export import phoenix.content.effects.types;
export import phoenix.content.equipment.types;

export namespace phoenix
{
FactionCatalog compile_factions(const Json &map);
void resolve_equipment(Catalog &catalog, SharedPropertyCatalog = {});
bool uses_advanced_machinery(const Catalog &, const std::string &entity);
void verify_gameplay_fitting(const Catalog &, const std::string &);
void verify_gameplay_fitting(const Catalog &, const std::string &, const DirectedCatalog &);
Json authored_equipment_source(Json);
} // namespace phoenix
