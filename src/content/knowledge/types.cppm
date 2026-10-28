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

export module phoenix.content.knowledge.types;
export import phoenix.data;
export import phoenix.knowledge;
export import phoenix.state_tree;
export import phoenix.content.controllers.types;

export namespace phoenix
{
struct GameplayKnowledgeConsoleDefinition
{
	ConsoleSpec console;
	std::vector<KnowledgeKey> readers, producers;
};
struct GameplayKnowledgeSharingDefinition
{
	std::string id, source, target;
	KnowledgeKey schema = 0;
	KnowledgeId subject = 0; // Zero binds to this ship when activated.
	std::vector<KnowledgeKey> fields;
	bool live = false;
};
struct GameplayKnowledgeShipDefinition
{
	std::vector<GameplayKnowledgeConsoleDefinition> consoles;
	std::map<std::string, std::string> owners;
	std::map<std::string, std::string> controllers;
	std::vector<GameplayKnowledgeSharingDefinition> feeds;
	std::map<std::string, GameplayKnowledgeSharingDefinition> publications;
	struct TelemetryField
	{
		KnowledgeKey field = 0;
		std::size_t value = 0;
		KnowledgeType type = KnowledgeType::Integer;
		bool required = true;
	};
	struct TelemetryRecord
	{
		KnowledgeKey schema = 0, producer = 0;
		KnowledgeId subject = 0;
		std::vector<TelemetryField> fields;
		EvidenceKind kind = EvidenceKind::Fact;
	};
	std::map<std::string, std::vector<TelemetryRecord>> telemetry;
	struct Reporter
	{
		std::string id, field, message, recovery_message;
		std::int64_t above = 0;
		KnowledgeTick debounce = 0, recovery = 0, minimum_gap = 0;
	};
	std::map<std::string, std::vector<Reporter>> reporters;
};
enum class GameplayKnowledgeBindingSource : std::uint8_t
{
	Argument,
	Observation,
	Intent,
	Memory,
	Parameter,
	Elapsed
};
struct GameplayKnowledgeMessageBinding
{
	GameplayKnowledgeBindingSource source = GameplayKnowledgeBindingSource::Argument;
	std::string name;
	// Missing accepts any supported scalar for a presentation parameter only.
	state_tree::Type type = state_tree::Type::Missing;
};
struct GameplayKnowledgeMessageDefinition
{
	KnowledgeKey schema = 0;
	MessageDestinationKind destination_kind = MessageDestinationKind::Ship;
	std::string destination_name;
	std::string destination_console;
	MessagePresentation presentation;
	KnowledgeTick delay = 1, expires_after = 0;
	KnowledgeId request_key = 0;
	bool human_seeking = false;
	std::map<KnowledgeKey, KnowledgeValue> literal_fields;
	std::vector<KnowledgeKey> bound_fields;
	std::vector<std::string> bound_parameters;
	std::map<KnowledgeKey, GameplayKnowledgeMessageBinding> field_bindings;
	std::map<std::string, GameplayKnowledgeMessageBinding> parameter_bindings;
};
struct GameplayKnowledgeMessageContext
{
	std::int64_t amount = 0, elapsed = 0;
	bool flag = false;
	std::string detail;
	const state_tree::Program *program = nullptr;
	const state_tree::Instance *instance = nullptr;
	const std::map<std::string, state_tree::Value> *parameters = nullptr;
	const std::map<std::string, state_tree::Value> *memory = nullptr, *captured_intents = nullptr;
	std::span<const state_tree::Value> observations, intents;
};
struct GameplayKnowledgeMessageValues
{
	std::map<KnowledgeKey, KnowledgeValue> fields;
	std::map<std::string, std::string> parameters;
};
struct GameplayKnowledgeDefinitions
{
	CompiledKnowledge knowledge;
	std::map<std::string, GameplayKnowledgeShipDefinition> ships;
	std::map<std::string, GameplayKnowledgeMessageDefinition> messages;
	std::map<std::string, KnowledgeId> request_keys;
	std::vector<std::string> telemetry_names;
	std::map<std::string, std::size_t> telemetry_indices;
};
} // namespace phoenix
