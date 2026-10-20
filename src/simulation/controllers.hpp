#pragma once
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
	std::string tree;
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
	std::uint64_t operator_claimant = 0;
	std::optional<ControllerBrain> brain;
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
