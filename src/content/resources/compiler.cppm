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

export module phoenix.content.resources;
export import phoenix.data;
export import phoenix.resources;
export import phoenix.content.catalog;
export import phoenix.content.combat.types;
export import phoenix.content.effects.types;
export import phoenix.content.resources.types;

export namespace phoenix
{
ResourceCatalogue compile_resource_catalog(const Catalog &);
std::map<std::string, RecipeDefinition> compile_recipe_catalog(const Catalog &);
RecipeDefinition compile_recipe(const Catalog &, const std::string &);
StockStoreDefinition compile_stock_store(const Catalog &, const Json &, const std::string &);
ProcessorDefinition compile_processor(const Catalog &, const Json &, const std::string &);
std::vector<ProcessorDefinition> compile_processors(const Catalog &, const std::string &);
std::vector<ProcessorDefinition> compile_processors(const Catalog &, const std::string &, const DirectedCatalog &,
                                                    SharedPropertyCatalog = {});
std::map<std::string, FtlDefinition> compile_ftl(const Catalog &catalog, const std::string &entity);
std::optional<ShipResourcesDefinition> compile_ship_resources(const Catalog &catalog, const std::string &entity);
std::map<std::string, SystemObservationSchema> compile_system_observations(const Catalog &, const std::string &entity);
// Reuse already compiled ship metadata when validating several Controller trees.
std::map<std::string, SystemObservationSchema>
compile_system_observations(std::span<const FittedSystemSpec>, const ShipResourcesDefinition *, bool advanced);
} // namespace phoenix
