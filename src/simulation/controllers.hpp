#pragma once
namespace phoenix
{
struct RecordReadBinding
{
	bool shared = false, latest = false;
	KnowledgeKey schema = 0, field = 0;
	KnowledgeId subject = 0;
};
struct ControllerBrain
{
	state_tree::Program program;
	std::string tree;
	std::vector<RecordReadBinding> reads;
	state_tree::Instance continuation;
	state_tree::Debug debug;
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
struct ConsoleControllerRuntime
{
	ConsoleControllerSpec definition;
	std::vector<ControllerChannelState> commands;
	bool human = false;
	std::uint64_t generation = 1;
	std::optional<Controller> timed;
	std::optional<ControllerBrain> brain;
	std::vector<std::size_t> tree_channels;
	std::vector<ControllerTelemetryBinding> telemetry_reads;
	std::vector<std::pair<std::size_t, std::size_t>> command_reads, contact_reads;
	std::vector<RecordReadBinding> message_reads;
	ActionResult result;
	std::set<std::string> effects;
};
struct EntityControls
{
	ControllableLayout layout;
	std::vector<ConsoleControllerRuntime> controllers;
	std::map<std::string, std::size_t> profiles;
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
