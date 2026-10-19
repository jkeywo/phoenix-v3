#pragma once
// Exported compile-only contracts. Include at module scope in phoenix.content,
// after Catalog/ConsoleSpec/resolve_consoles have been declared.
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
};
struct GameplayKnowledgeMessageDefinition
{
	KnowledgeKey schema = 0;
	MessageDestinationKind destination_kind = MessageDestinationKind::Ship;
	std::string destination_name;
	MessagePresentation presentation;
	KnowledgeTick delay = 1, expires_after = 0;
	KnowledgeId request_key = 0;
	std::map<KnowledgeKey, KnowledgeValue> literal_fields;
	std::vector<KnowledgeKey> bound_fields;
	std::vector<std::string> bound_parameters;
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
GameplayKnowledgeDefinitions compile_gameplay_knowledge(const Catalog &catalog);
} // namespace phoenix
