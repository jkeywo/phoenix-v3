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
	Json commands, equipment, data, operations;
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
void expand_control_widget_policies(Catalog &);
const Json &compiled_widget_instance(const Catalog &, const std::string &entity, const Json &instance);
Json bind_widget_policy(const Catalog &, const Json &policy, const Json &equipment, const std::string &where);
BridgeDefinition compile_bridge(const Catalog &, const std::string &entity);
ConsoleBridgeDefinition compile_console_bridge(const Json &, const std::string &path);
// Compile shared logical widget instances and their star mappings.
ControllableLayout compile_controllable_layout(const Catalog &, const std::string &entity, SharedPropertyCatalog = {});
std::vector<FittedSystemSpec> resolve_systems(const Catalog &catalog, const std::string &entity);
// Resolves legacy single-console content and verifies explicit station ownership.
std::vector<ConsoleSpec> resolve_consoles(const Catalog &catalog, const std::string &entity);
} // namespace phoenix
