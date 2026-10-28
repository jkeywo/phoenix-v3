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

export module phoenix.content.logistics;
export import phoenix.data;
export import phoenix.content.catalog;
export import phoenix.content.combat.types;
export import phoenix.content.effects.types;
export import phoenix.content.logistics.types;

export namespace phoenix
{
LogisticsDefinition compile_logistics(const Catalog &, const std::string &);
LogisticsDefinition compile_logistics(const Catalog &, const std::string &, const DirectedCatalog &,
                                      SharedPropertyCatalog = {});
std::vector<StartingStockDefinition> compile_starting_stocks(const Catalog &, const Json &, const DirectedCatalog &,
                                                             SharedPropertyCatalog = {});
std::vector<DockPortDefinition> compile_docking(const Catalog &, const std::string &);
std::vector<DockPortDefinition> compile_docking(const Catalog &, const std::string &, const DirectedCatalog &,
                                                SharedPropertyCatalog = {});
void expand_transport_fittings(Catalog &, SharedPropertyCatalog = {});
TransportDefinition compile_transport(const Catalog &, const std::string &);
TransportDefinition compile_transport(const Catalog &, const std::string &, const DirectedCatalog &,
                                      SharedPropertyCatalog = {});
std::optional<CraftDefinition> compile_craft(const Catalog &, const std::string &);
std::map<std::string, CraftBayDefinition> compile_craft_bays(const Catalog &, const std::string &);
std::map<std::string, CraftBayDefinition> compile_craft_bays(const Catalog &, const std::string &,
                                                             const DirectedCatalog &, SharedPropertyCatalog = {});
} // namespace phoenix
