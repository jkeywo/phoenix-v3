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

export module phoenix.content.knowledge;
export import phoenix.data;
export import phoenix.state_tree;
export import phoenix.gameplay_vocabulary;
export import phoenix.content.catalog;
export import phoenix.content.controllers.types;
export import phoenix.content.effects.types;
export import phoenix.content.knowledge.types;

export namespace phoenix
{
Json resolve_tree_definition(const Json &, const Catalog &);
std::vector<GameplayTreeRecordBinding> gameplay_record_bindings(const Json &, const Catalog &);
std::vector<GameplayTreeSummaryBinding> gameplay_summary_bindings(const Json &, const Catalog &);
state_tree::Registry gameplay_registry(const Json &, const Catalog &, state_tree::Context);
bool supported_system_observation(const std::string &);
state_tree::Type system_observation_type(const std::string &);
std::vector<ControllerTelemetryBinding> controller_telemetry_bindings(const Json &, const Catalog &);
std::vector<ControllerCommandReadBinding> controller_command_reads(const Json &, const Catalog &);
std::vector<ControllerContactBinding> controller_contact_bindings(const Json &, const Catalog &);
std::vector<std::string> controller_ability_bindings(const Json &, const Catalog &, SharedPropertyCatalog = {});
std::vector<std::string> controller_process_bindings(const Json &, const Catalog &, SharedPropertyCatalog = {});
std::vector<std::string> controller_message_templates(const Json &, const Catalog &);
std::vector<GameplayTreeRecordBinding> controller_message_reads(const Json &, const Catalog &);
ControllerTreeBindings compile_controller_bindings(const Json &, const Catalog &, SharedPropertyCatalog = {});
// Includes standard capability channels and the fitted controller's inherited declarations.
std::vector<std::string> gameplay_summary_channels(const Catalog &, const std::string &entity,
                                                   const std::string &system);
// Caller supplies the active task's compiled call frame and immutable permitted
// snapshot. No world lookup or JSON expression dispatch occurs here.
GameplayKnowledgeMessageValues evaluate_gameplay_message(const GameplayKnowledgeMessageDefinition &message,
                                                         const GameplayKnowledgeMessageContext &context);
GameplayKnowledgeDefinitions compile_gameplay_knowledge(const Catalog &catalog, SharedPropertyCatalog = {});
} // namespace phoenix
