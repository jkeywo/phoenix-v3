module;
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

export module phoenix.control.controllers;
export import phoenix.content.controllers.types;
export import phoenix.control.candidate_facts;
export import phoenix.knowledge.decision_machinery;

export namespace phoenix
{
std::string controller_report_key(const std::string &, const KnowledgeRecord &);
std::string controller_report_fingerprint(const KnowledgeRecord &);
struct RecordReadBinding
{
	bool shared = false, latest = false;
	KnowledgeKey schema = 0, field = 0;
	KnowledgeId subject = 0;
};
struct RuntimeRecordRead : RecordReadBinding
{
	std::size_t destination = 0;
	ControllerDestinations<ControllerRecordField> metadata;
};
struct RuntimeTelemetryRead
{
	ControllerTelemetryBinding source;
	std::size_t destination = 0;
	std::optional<TelemetryField> field;
};
struct RuntimeAbilityBinding : ControllerAbilityBinding
{
	std::size_t endpoint_index = static_cast<std::size_t>(-1);
};
struct RuntimeProcessBinding : ControllerProcessBinding
{
	std::size_t endpoint_index = static_cast<std::size_t>(-1);
};
struct RuntimeCommandRead
{
	std::size_t controller = 0, channel = 0, destination = 0;
	ControllerCommandReadSource source;
};
struct StagedCommandRead
{
	std::size_t channel = 0, argument = 0;
	ControllerCommandReadField field = ControllerCommandReadField::Value;
};
struct RuntimeContactRead
{
	std::size_t controller = 0, channel = 0;
	ControllerDestinations<ControllerContactField> fields;
};
struct RuntimeCommandBinding : ControllerCommandBinding
{
	std::size_t channel_index = 0;
};
struct CompiledObjectiveObservation
{
	std::size_t destination = 0;
	ObjectiveField semantic = ObjectiveField::Count;
	KnowledgeKey field = 0;
	state_tree::Type type = state_tree::Type::Missing;
};
struct CompiledReportField
{
	KnowledgeKey key = 0;
	KnowledgeType type = KnowledgeType::Integer;
	bool required = false, selected = false;
	std::optional<std::size_t> value_slot, available_slot;
};
struct RuntimeObjectiveBinding
{
	CandidateSource source = CandidateSource::single;
	std::optional<std::size_t> action;
	KnowledgeKey distance_parameter = 0, report_schema = 0;
	KnowledgeTick report_stale_after = 0;
	std::array<KnowledgeKey, 4> report_scope{};
	std::vector<CompiledReportField> report_fields;
};
enum class ControllerCandidateOperationKind
{
	Select,
	Replace,
	Cancel,
	Acknowledge,
	Coordination
};
struct RuntimeCandidateOperationBinding
{
	ControllerCandidateOperationKind kind = ControllerCandidateOperationKind::Coordination;
	std::string name;
	std::optional<std::size_t> action;
};
struct ControllerObservationLayout
{
	// Immutable schema addresses, shared by copies of a compiled template.
	std::map<std::string, std::size_t, std::less<>> fields;
	std::vector<CompiledObjectiveObservation> compiled_objective_fields;
	KnowledgeKey objective_schema = 0;
	std::array<KnowledgeKey, static_cast<std::size_t>(ObjectiveField::Count)> objective_keys{};
	std::array<std::optional<std::size_t>, static_cast<std::size_t>(CandidateField::Count)> candidate_slots{};
	std::array<std::optional<std::size_t>, 5> docking{};
	std::array<std::optional<std::size_t>, 3> action_inputs{};
	std::array<std::optional<std::size_t>, 2> manoeuvre{};
	std::optional<std::size_t> bay_accepted_target;
	std::optional<std::size_t> summary_level, objective_status;
	std::array<std::optional<std::size_t>, 4> travel;
};
struct ControllerBrain
{
	state_tree::Program program;
	std::string tree, model_context;
	std::string ftl_system;
	std::optional<std::size_t> logistics_revision, docking_revision;
	std::optional<std::size_t> complexity_stars;
	std::optional<std::size_t> workflow_base, medical_base;
	std::string routine_stance = "routine";
	std::size_t decision_base = 0, action_base = 0, coordination_base = 0;
	bool report_observations = false;
	std::map<std::string, std::array<std::int64_t, 6>> objective_scoring;
	std::array<std::int64_t, 6> default_objective_scoring{1, 1, 1, 1, 1, 100};
	std::vector<ControllerObjectiveBinding> objective_bindings;
	std::array<bool, 6> request_support{};
	std::vector<RuntimeObjectiveBinding> compiled_objectives;
	std::vector<std::vector<std::size_t>> action_write_channels;
	std::vector<std::map<state_tree::Index, RuntimeCandidateOperationBinding>> candidate_operations;
	ControllerDestinations<ControllerOwnField> own;
	ControllerDestinations<ControllerInboxField> inbox;
	ControllerDestinations<ControllerResultField> result;
	std::vector<RuntimeCommandBinding> commands;
	std::vector<RuntimeRecordRead> reads, message_reads;
	std::vector<RuntimeTelemetryRead> telemetry_reads;
	std::vector<RuntimeAbilityBinding> abilities;
	std::vector<RuntimeProcessBinding> processes;
	std::vector<RuntimeCommandRead> command_reads;
	std::map<state_tree::Index, StagedCommandRead> staged_command_reads;
	std::vector<RuntimeContactRead> contact_reads;
	state_tree::Instance continuation;
	state_tree::Debug debug;
	std::shared_ptr<const ControllerObservationLayout> observation_layout;
	std::optional<std::size_t> observation_index(std::string_view name) const;
};
struct ControllerActionInstance
{
	std::string id, invocation, status = "idle", reason;
	std::vector<state_tree::Value> inputs;
	state_tree::Instance continuation;
	std::uint64_t serial = 0;
};
struct ActionResult
{
	bool available = false, success = false;
	std::string reason;
	std::uint64_t id = 0, publication = 0, message = 0;
};
struct ControllerChannelState
{
	std::int64_t value = 0;
	std::uint64_t revision = 0, consumed = 0, request_id = 0;
	bool accepted = true;
	std::vector<std::int64_t> arguments;
	bool target_override = false;
};
enum class OfficerOperation
{
	Power,
	Cooling,
	Manoeuvre,
	Repair,
	Manifest,
	Scan
};
enum class OfficerStatus
{
	Requested,
	Accepted,
	Refused,
	Completed,
	Failed,
	Cancelled
};
struct OfficerRequest
{
	KnowledgeId id = 0, peer = 0;
	OfficerOperation operation = OfficerOperation::Power;
	OfficerStatus status = OfficerStatus::Requested;
	std::int64_t target = 0, amount = 0, priority = 0;
	std::int64_t requested_amount = 0, retry_ticks = 0;
	KnowledgeTick expires = 0, reply_tick = 0;
	std::string reason, mission, capability;
	std::uint64_t request_revision = 0, evidence_revision = 0;
};
struct ControllerObjectiveRetry
{
	std::uint64_t not_before = 0, attempt = 0, failed_serial = 0;
	std::string action;
	std::vector<state_tree::Value> arguments;
};
struct ControllerReportReceipt
{
	KnowledgeTick sent_tick = 0, observed_tick = 0, revision = 0;
	std::string fingerprint;
};
struct ModelOrderBasis
{
	std::uint64_t revision = 0, priority_revision = 0, alert = 0;
	std::set<KnowledgeId> priorities;
	bool operator==(const ModelOrderBasis &) const = default;
};
struct ModelDeviationReceipt
{
	ModelOrderBasis orders;
	std::string reason;
};
struct ConsoleControllerRuntime
{
	ConsoleControllerSpec definition;
	std::vector<ControllerChannelState> commands;
	bool human = false;
	// Authoritative per command. human is a derived presentation summary.
	std::vector<bool> human_commands;
	std::set<std::string> human_operations;
	std::uint64_t generation = 1;
	std::uint64_t operator_claimant = 0;
	std::optional<ControllerBrain> brain;
	ActionResult result;
	std::set<std::string> effects;
	std::vector<ControllerCandidateSpec> candidates;
	std::string selected_provider = "tree", effective_provider = "tree", fallback_reason;
	std::uint64_t provider_generation = 1, decision_sequence = 0, next_decision = 0;
	std::uint64_t retry_count = 0, last_decision_tick = 0;
	Json decision_history = Json::array();
	std::vector<ModelDeviationReceipt> deviation_receipts;
	KnowledgeId selected_objective = 0;
	std::string objective_commitment;
	std::optional<ControllerObjectiveChoice> objective_choice; // Frozen, recomputed before each policy tick.
	std::map<std::string, ControllerObjectiveRetry> objective_retries;
	std::map<std::string, ControllerReportReceipt> report_receipts;
	Json objective_diagnostics = Json::array();
	std::vector<ControllerActionSpec> actions;
	ControllerActionInstance action;
	// Transient: execution writes are consumed in this tick, before any save boundary.
	std::set<std::size_t> action_writes;
	std::vector<OfficerRequest> incoming_requests, outgoing_requests;
	// Protocol deliveries processed behind unread ordinary messages cannot advance
	// the cumulative inbox cursor. Keep receipts until those deliveries leave it.
	std::set<KnowledgeId> coordination_receipts;
};
struct EntityControls
{
	ControllableLayout layout;
	std::vector<ConsoleControllerRuntime> controllers;
	std::map<std::string, std::size_t> profiles;
};
// Accepted ship/Console intent blackboards; never physical actuator state.
struct NavigationEvidence
{
	std::uint64_t observed_tick = 0;
	bool blocked = false, lateral_safe = false;
	std::int64_t left_heading = 0, lateral_thrust = 0;
};
struct BridgeBlackboard
{
	std::size_t condition = 0;
	std::uint64_t revision = 0;
	std::map<std::string, std::size_t> stances;
	std::map<std::string, std::uint64_t> epochs;
	NavigationEvidence navigation;
	std::set<KnowledgeId> priority_objectives;
	std::uint64_t priority_revision = 0;
};
ModelOrderBasis model_order_basis(const BridgeBlackboard &, const ConsoleControllerRuntime &);
void record_model_deviation_receipt(ConsoleControllerRuntime &, ModelOrderBasis, std::string reason);
void restore_model_deviation_receipts(ConsoleControllerRuntime &, const Json &history);
bool model_deviation_reported(const ConsoleControllerRuntime &, std::string_view reason);
struct ControllerProposal
{
	ControllerAddress address;
	std::size_t controller = 0, channel = 0;
	std::int64_t value = 0;
	std::uint64_t generation = 0, request_id = 0;
	bool human = false;
	std::vector<std::int64_t> arguments;
	std::string operation; // Trusted native composition; never accepted from a scalar wire command.
};
// Policy-independent proposal; admission and native execution retain authority.
struct ControllerDecisionProposal
{
	ControllerAddress address;
	std::uint64_t generation = 0;
	std::string operation = "select", action, invocation;
	std::vector<state_tree::Value> inputs;
};

const ControllerActionSpec &controller_action_spec(const ConsoleControllerRuntime &);
bool controller_command_human(const ConsoleControllerRuntime &, std::size_t channel);
bool controller_operation_human(const ConsoleControllerRuntime &, const std::string &operation);
bool controller_operation_owns(const ConsoleControllerRuntime &, const std::string &operation,
                               const std::string &equipment);
bool controller_ai_running(const ConsoleControllerRuntime &);
Json save_controller_action(const ConsoleControllerRuntime &);
void restore_controller_action(ConsoleControllerRuntime &, const Json &);
void set_console_complexity(EntityControls &, const std::string &console, std::size_t profile);
} // namespace phoenix
