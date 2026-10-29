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

export module phoenix.content.catalog;
export import phoenix.data;
export import phoenix.resources;
export import phoenix.knowledge;
export import phoenix.scenario;
export import phoenix.state_tree;
export import phoenix.conversations;
export import phoenix.content.controllers.types;
export import phoenix.content.packs;

export namespace phoenix
{
struct Catalog;
struct BaseConfiguration
{
	std::string id, hash, render_mode = "sprites", interface_style = "current";
	bool operator==(const BaseConfiguration &) const = default;
};
struct Catalog
{
	std::map<std::string, Json> entities, maps, consoles, trees;
	std::map<std::string, Json> layouts, ui_groups, themes;
	std::map<std::string, Json> control_widgets;
	std::map<std::string, Json> equipment, assemblies, messages, effects, resources, recipes, properties;
	Json knowledge = Json::object();
	CompiledKnowledge knowledge_definition;
	std::map<std::string, ResourceNetworkDefinition> resource_definitions;
	std::map<std::string, ScenarioDefinition> scenarios;
	std::map<std::string, state_tree::Program> tree_programs;
	std::map<std::string, ConversationProgram> conversation_programs;
	std::map<std::string, ControllerTreeBindings> controller_bindings;
	std::map<std::string, std::map<std::pair<std::string, std::string>, std::string>> controller_policies;
	std::map<std::string, std::map<std::pair<std::string, std::string>, Json>> widget_instances;
	std::map<std::string, Bytes> assets;
	std::vector<std::pair<std::string, std::string>> ordered_packs;
	bool presentation = false, selection = false;
	std::string loading_background;
	std::string ui_theme;
	BaseConfiguration base;
};
// Resolution only; a root restricts traversal to its dependency closure.
void inherit_definitions(std::map<std::string, Json> &, const std::string &kind, const std::string &root = {});
// Parent is the required dependency for rooted packs. Legacy synthetic fixtures
// may still use the old dependency vocabulary outside a launched base tree.
std::vector<std::string> pack_dependencies(const Json &manifest);
BaseConfiguration base_configuration(const Pack &pack);
void overlay_knowledge(Json &, const Json &, const std::string &);
} // namespace phoenix
