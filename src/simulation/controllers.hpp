#pragma once
namespace phoenix
{
struct ControllerChannelState
{
	std::int64_t value = 0;
	std::uint64_t revision = 0, consumed = 0, request_id = 0;
	bool accepted = true;
};
struct ConsoleControllerRuntime
{
	ConsoleControllerSpec definition;
	std::vector<ControllerChannelState> commands;
	bool human = false;
	std::uint64_t generation = 1;
	std::optional<Controller> timed;
	std::optional<BrainRuntime> brain;
	std::vector<std::size_t> tree_channels;
	std::vector<ControllerTelemetryBinding> telemetry_reads;
	ActionResult result;
	std::map<std::string, std::int64_t> summary;
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
};
} // namespace phoenix
