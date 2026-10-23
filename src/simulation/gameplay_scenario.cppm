module;
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module phoenix.gameplay_scenario;
export import phoenix.content;

export namespace phoenix
{
struct ScenarioTreeSelectors
{
	std::optional<ScenarioActor> actor;
	std::string objective, region, event, knowledge_console;
};
struct ScenarioKnowledgeReader
{
	ScenarioId instance = 0;
	EntityId ship = 0;
	std::string console;
};
struct ScenarioKnowledgeSnapshot
{
	ScenarioKnowledgeReader reader;
	std::vector<KnowledgeRecord> records, shared;
};
enum class ScenarioShieldEffectOperation
{
	Apply,
	Refresh,
	Remove
};
struct ScenarioShieldEffectRequest
{
	ScenarioActionToken token;
	ScenarioShieldEffectOperation operation = ScenarioShieldEffectOperation::Apply;
	EntityId target = 0;
	std::string definition, grant;
};
struct ScenarioTreeReceipt
{
	ScenarioActionToken token;
	GameplayTreeAction action = GameplayTreeAction::Count;
	ScenarioActionResult result;
	std::string request;
};
struct ScenarioTreeController
{
	std::string tree;
	state_tree::Program program;
	state_tree::Instance continuation;
	ScenarioTreeSelectors selectors;
	ScenarioId event_cursor = 0;
	std::optional<ScenarioActionToken> last_receipt;
	state_tree::Debug debug;
};
struct ScenarioSituationConfiguration
{
	std::string tree;
	std::map<std::string, ScenarioParameter> parameters;
	ScenarioTreeSelectors selectors;
};
// Authoritative adapter: cached production definitions + contextual evaluator +
// admitted domain effects. It does not construct physics or expose hidden state
// to player/AI projections. World owns physical creation and supplies motion.
class GameplayScenarioBridge
{
public:
	void load(const Catalog &catalog, const std::string &map);
	ScenarioId create_instance(std::string tree, std::string name,
	                           std::map<std::string, ScenarioParameter> parameters = {},
	                           ScenarioTreeSelectors selectors = {});
	void begin_tick(ScenarioTick tick, std::map<std::string, EntityId> slots, std::span<const BodyView> bodies);
	// Supply only the selected Console's permission-filtered public projection.
	// Copies freeze this tick's evidence before any scenario controller executes.
	std::vector<ScenarioKnowledgeReader> knowledge_readers() const;
	void supply_knowledge(std::span<const ScenarioKnowledgeSnapshot> snapshots);
	// Exactly one controller evaluation per committed tick; due event snapshots
	// are captured for every situation before any situation effects are executed.
	void step(std::uint64_t ticks = 1);
	ScenarioTreeReceipt execute(ScenarioId instance, const state_tree::Output &output);
	ScenarioTreeReceipt execute(ScenarioActionToken token, GameplayTreeAction action,
	                            std::vector<state_tree::Value> arguments);
	std::optional<ScenarioTreeReceipt> receipt(ScenarioActionToken token) const;
	std::vector<ScenarioShieldEffectRequest> pending_shield_effects() const;
	void confirm_shield_effect(ScenarioActionToken token, std::string failure = {});
	std::vector<ScenarioSpawnRequest> pending_spawns() const;
	void confirm_spawn(ScenarioId request, std::span<const EntityId> ids, std::string failure = {});
	std::vector<ScenarioMotionSegment> sample_motion(std::span<const BodyView> bodies) const;
	void evaluate(std::span<const ScenarioMotionSegment> executed_motion);
	void synchronize_time(ScenarioTick tick);
	void evaluate_conditions(ScenarioTick tick, const std::map<ScenarioId, ScenarioFacts> &snapshots);
	std::vector<state_tree::Value> observations(ScenarioId instance) const;
	ScenarioRuntime &runtime();
	const ScenarioRuntime &runtime() const;
	const std::map<ScenarioId, ScenarioTreeController> &controllers() const;
	Json save() const;
	// World-owned event sources have no state-tree Controller.
	void restore(const Catalog &catalog, const std::string &map, const Json &saved, ScenarioId event_source = 0);

private:
	ScenarioRuntime runtime_;
	std::map<std::string, Json> tree_sources_;
	std::map<std::string, state_tree::Program> programs_;
	std::map<std::string, ScenarioSituationConfiguration> situations_;
	std::map<ScenarioId, ScenarioTreeController> controllers_;
	std::map<ScenarioActionToken, ScenarioTreeReceipt> receipts_;
	std::map<ScenarioActionToken, ScenarioShieldEffectRequest> shield_requests_;
	std::map<EntityId, BodyView> bodies_;
	CompiledKnowledge knowledge_;
	std::map<std::string, std::vector<GameplayTreeRecordBinding>> record_bindings_;
	std::map<std::string, std::map<std::string, std::vector<KnowledgeKey>>> reader_contracts_;
	std::map<ScenarioId, ScenarioKnowledgeSnapshot> knowledge_snapshots_;
	bool knowledge_supplied_ = false;
	std::string map_, contract_;
	ScenarioTick tick_ = 0;
	std::optional<ScenarioTick> stepped_tick_;
	state_tree::Program instantiate(const std::string &tree,
	                                const std::map<std::string, ScenarioParameter> &parameters) const;
	std::vector<state_tree::Value> snapshot(ScenarioId instance, ScenarioId *event_cursor) const;
	std::optional<ScenarioId> selected_objective(ScenarioId instance) const;
	void validate_selectors(const ScenarioTreeSelectors &selectors) const;
	void validate_reader(const std::string &tree, const ScenarioTreeSelectors &selectors) const;
	void step_transaction(std::uint64_t ticks);
};
} // namespace phoenix
