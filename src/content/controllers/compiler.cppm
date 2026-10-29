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

export module phoenix.content.controllers;
export import phoenix.data;
export import phoenix.state_tree;
export import phoenix.content.catalog;
export import phoenix.content.combat.types;
export import phoenix.content.controllers.types;
export import phoenix.content.effects.types;
export import phoenix.content.resources.types;

export namespace phoenix
{
struct ControlWidgetLevel
{
	std::string id;
	std::set<std::string> human;
	std::map<std::string, std::vector<std::string>> replacements;
	Json data;
};
struct ControlWidgetDefinition
{
	std::string id, tree;
	Json commands, equipment, data;
	std::vector<ControlWidgetLevel> levels;
	std::set<std::string> parts;
};
ControlWidgetDefinition compile_control_widget(const Catalog &, const std::string &id);
Json bind_control_widget(const ControlWidgetDefinition &, const Json &instance);
std::map<std::string, WidgetDataDestination> control_widget_data_placement(const ControlWidgetDefinition &,
                                                                           const std::string &level, int stars,
                                                                           const Json &overrides = Json::object());
std::vector<ControllerCandidateSpec> compile_controller_candidates(const Json &, const Catalog &,
                                                                   const state_tree::Registry &);
std::vector<ControllerActionSpec> compile_controller_actions(const Json &, const Catalog &,
                                                             const state_tree::Registry &);
void expand_combat_policies(Catalog &);
void expand_officer_policies(Catalog &);
BridgeDefinition compile_bridge(const Catalog &, const std::string &entity);
ConsoleBridgeDefinition compile_console_bridge(const Json &, const std::string &path);
// Compiles explicit layout version 1 or converts unambiguous old ownership at the boundary.
ControllableLayout compile_controllable_layout(const Catalog &, const std::string &entity, SharedPropertyCatalog = {});
// Boundary conversion only. Unsupported decision/ownership mappings fail before runtime creation.
Catalog convert_controller_content(const Catalog &, SharedPropertyCatalog = {});
Catalog convert_controller_content(const Catalog &, const DirectedCatalog &, SharedPropertyCatalog = {});
std::vector<FittedSystemSpec> resolve_systems(const Catalog &catalog, const std::string &entity);
// Resolves legacy single-console content and verifies explicit station ownership.
std::vector<ConsoleSpec> resolve_consoles(const Catalog &catalog, const std::string &entity);
} // namespace phoenix
