module;
#include <nlohmann/json.hpp>
#include <span>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

export module phoenix.control.views;
export import phoenix.control.controllers;

export namespace phoenix
{
// Physical consumer facts are partitioned by command ownership, including shared widgets.
struct ControllerPhysicalGrantView
{
	std::string console, controller;
	bool human = false;
	std::uint64_t generation = 0;
	std::vector<std::size_t> physical_consumers;
};
std::vector<ControllerPhysicalGrantView> controller_physical_grants(std::span<const ConsoleControllerRuntime>);
struct ControllerViewFacts
{
	std::vector<std::string> systems;
	bool conversation_authority = false, contribution_authority = false, craft_authority = false,
	     transport_authority = false;
};
Json project_controller_runtime(const ConsoleControllerRuntime &, const ControllerViewFacts &,
                                std::span<const state_tree::Value> action_observations);
} // namespace phoenix
