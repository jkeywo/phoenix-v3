#pragma once
#include "effect_receipts.hpp"
namespace phoenix
{
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
struct RuntimeCommandRead
{
	std::size_t controller = 0, channel = 0, destination = 0;
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
struct ControllerBrain
{
	state_tree::Program program;
	std::string tree, model_context;
	std::string ftl_system;
	std::optional<std::size_t> logistics_revision, docking_revision;
	std::string routine_stance = "routine";
	std::size_t decision_base = 0, action_base = 0, coordination_base = 0;
	ControllerDestinations<ControllerOwnField> own;
	ControllerDestinations<ControllerInboxField> inbox;
	ControllerDestinations<ControllerResultField> result;
	std::vector<RuntimeCommandBinding> commands;
	std::vector<RuntimeRecordRead> reads, message_reads;
	std::vector<ControllerRead<ControllerTelemetryBinding>> telemetry_reads;
	std::vector<RuntimeCommandRead> command_reads;
	std::vector<RuntimeContactRead> contact_reads;
	state_tree::Instance continuation;
	state_tree::Debug debug;
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
};
enum class OfficerOperation
{
	Power,
	Cooling,
	Manoeuvre,
	Repair
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
	std::string reason;
};
struct ConsoleControllerRuntime
{
	ConsoleControllerSpec definition;
	std::vector<ControllerChannelState> commands;
	bool human = false;
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
};
struct ControllerProposal
{
	ControllerAddress address;
	std::size_t controller = 0, channel = 0;
	std::int64_t value = 0;
	std::uint64_t generation = 0, request_id = 0;
	bool human = false;
	std::vector<std::int64_t> arguments;
};
} // namespace phoenix
