#pragma once
// Private phoenix.simulation declarations. Include after module imports; never
// include this header in player/Workshop targets.
namespace phoenix
{
struct GameplayKnowledgeProjection
{
	KnowledgeId console = 0;
	std::vector<KnowledgeRecord> records, shared;
	std::vector<CoordinationDelivery> messages;
	std::vector<CoordinationDelivery> notifications;
};
struct GameplayKnowledgeShipBinding
{
	std::string definition;
	std::map<std::string, KnowledgeId> consoles, stations, systems;
};
class GameplayKnowledge
{
public:
	// Compile all bindings once through resolved production definitions. configure
	// resets running state and must run before register_ship/restore.
	void configure(const Catalog &catalog);
	void register_ship(EntityId ship, const std::string &definition, KnowledgeTick tick);
	bool contains_ship(EntityId ship) const;
	KnowledgeId console_id(const ConsoleAddress &address) const;
	KnowledgeId system_id(EntityId ship, const std::string &system) const;
	KnowledgeId ai_id(EntityId ship, const std::string &system) const;
	ConsoleAddress address(KnowledgeId console) const;
	const CompiledKnowledge &definition() const;
	const GameplayKnowledgeMessageDefinition &message_definition(const std::string &name) const;
	KnowledgeKey message_producer(const ConsoleAddress &source, const std::string &sender_system, KnowledgeKey schema,
	                              ControlOrigin origin) const;
	KnowledgeId request_key(const std::string &name) const;
	std::string request_key_name(KnowledgeId key) const;
	// human_systems is the effective owned subset after delegation/preset changes.
	void set_console_control(const ConsoleAddress &address, bool human_present,
	                         const std::vector<std::string> &human_systems = {});
	void clear_control();
	// Trusted observation ingestion. System observations fan out only to their
	// owning Console and its explicit authored feed recipients; roles still apply.
	std::size_t ingest_system(EntityId ship, const std::string &system, KnowledgeRecordInput value, KnowledgeTick tick);
	bool ingest_console(const ConsoleAddress &address, KnowledgeRecordInput value, KnowledgeTick tick);
	const std::vector<std::string> &telemetry_names() const;
	std::size_t ingest_telemetry(EntityId ship, const std::string &system,
	                             const std::vector<std::optional<KnowledgeValue>> &values, KnowledgeTick tick);
	std::size_t ingest_telemetry(EntityId ship, const std::string &system,
	                             const std::map<std::string, std::int64_t> &values, KnowledgeTick tick);
	// Trusted scenario/objective publication. Human admission must never call it.
	bool publish_ship(EntityId ship, KnowledgeRecordInput value, KnowledgeTick tick);
	bool publish_scenario_console(const ConsoleAddress &address, KnowledgeRecordInput value, KnowledgeTick tick);
	KnowledgeId activate_publication(const ConsoleAddress &address, const std::string &name, KnowledgeTick tick);
	KnowledgeId activate_publication(const ConsoleAddress &address, const std::string &name, KnowledgeTick tick,
	                                 KnowledgeId subject, bool live, const std::string &request_key);
	KnowledgeId publish(const ConsoleAddress &address, KnowledgeKey schema, KnowledgeId subject,
	                    std::vector<KnowledgeKey> fields, bool live, KnowledgeTick tick);
	KnowledgeId activate_feed(const ConsoleAddress &source, const ConsoleAddress &target, KnowledgeKey schema,
	                          KnowledgeId subject, std::vector<KnowledgeKey> fields, KnowledgeTick tick);
	void stop_publication(const ConsoleAddress &address, KnowledgeId publication);
	// Human and AI callers pass their actual origin, then use the same authority
	// gate. The source address comes from World claims/tree ownership, never wire input.
	KnowledgeId admit_message(const ConsoleAddress &source, CoordinationInput value, KnowledgeTick tick);
	KnowledgeId admit_template(const ConsoleAddress &source, const std::string &sender_system,
	                           const std::string &message, KnowledgeRecordInput payload, ControlOrigin origin,
	                           KnowledgeTick tick, std::map<std::string, std::string> parameters = {});
	KnowledgeId publish_scenario_message(const ConsoleAddress &source, CoordinationInput value, KnowledgeTick tick);
	bool withdraw_request(const ConsoleAddress &source, const std::string &sender_system, KnowledgeId request_key,
	                      ControlOrigin origin);
	std::vector<CoordinationDelivery> tick(KnowledgeTick tick);
	std::vector<CoordinationMessage> pending_messages(std::size_t limit = 32) const;
	std::vector<CoordinationDelivery> receive_system(EntityId ship, const std::string &system);
	std::vector<CoordinationDelivery> peek_system(EntityId ship, const std::string &system);
	void acknowledge_system(EntityId ship, const std::string &system, KnowledgeId through_message);
	KnowledgePrivateMemory &private_memory(EntityId ship, const std::string &system);
	bool status_change(EntityId ship, const std::string &system, KnowledgeKey status, bool value, KnowledgeTick tick,
	                   KnowledgeTick debounce, KnowledgeTick recovery_hysteresis, KnowledgeTick minimum_gap = 0);
	bool stale(const KnowledgeRecord &record, KnowledgeTick now) const;
	GameplayKnowledgeProjection project(const ConsoleAddress &address) const;
	Json project_json(const ConsoleAddress &address) const;
	Json save() const;
	// Call configure first. Rebuilds named identities, verifies authored ownership,
	// restores continuations, and clears live human authority before any delivery.
	void restore(const Json &state);

private:
	CompiledKnowledge compiled_;
	KnowledgeRuntime runtime_;
	std::map<std::string, GameplayKnowledgeShipDefinition> definitions_;
	std::map<std::string, GameplayKnowledgeMessageDefinition> messages_;
	std::map<std::string, KnowledgeId> request_keys_;
	std::vector<std::string> telemetry_names_;
	std::map<std::string, std::size_t> telemetry_indices_;
	std::map<EntityId, GameplayKnowledgeShipBinding> ships_;
	std::map<KnowledgeId, ConsoleAddress> addresses_;
	std::map<KnowledgeId, bool> human_consoles_, human_systems_;
	std::map<KnowledgeId, KnowledgeId> publication_owners_;
	std::map<KnowledgeId, std::vector<CoordinationDelivery>> notifications_;
	KnowledgeId next_binding_ = 1;
	const GameplayKnowledgeConsoleDefinition &console_definition(const ConsoleAddress &address) const;
	bool readable(const GameplayKnowledgeConsoleDefinition &console, KnowledgeKey schema) const;
	void admit_sender(const ConsoleAddress &source, KnowledgeId sender, KnowledgeKey producer, ControlOrigin origin,
	                  bool known_record = false) const;
	bool known_evidence(const ConsoleAddress &source, const KnowledgeRecordInput &value) const;
	void install_ship(EntityId ship, GameplayKnowledgeShipBinding binding, KnowledgeTick tick, bool create_feeds);
	KnowledgeId binding_id();
};
} // namespace phoenix
