module;
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <span>
#include <set>
#include <string>
#include <vector>

export module phoenix.control.handover;
export import phoenix.control.controllers;

export namespace phoenix
{
struct ControllerHandoverPorts
{
	std::function<std::vector<state_tree::Value>()> observations;
	std::function<void(std::span<const state_tree::Value>)> cancel;
	std::function<void(const state_tree::Output &, std::span<const state_tree::Value>)> output;
	std::function<void(const OfficerRequest &)> reply;
	std::function<void()> advance_epoch;
	std::function<void(bool present, bool human)> control;
};
void refresh_controller_operator(ConsoleControllerRuntime &, std::uint64_t claimant, bool human, bool validate,
                                 const ControllerHandoverPorts &, std::vector<bool> human_commands = {},
                                 std::set<std::string> human_operations = {});
void advance_controller_stance_epoch(BridgeBlackboard &, const std::string &console);
} // namespace phoenix
