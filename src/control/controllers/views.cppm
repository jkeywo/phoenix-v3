module;
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <vector>

export module phoenix.control.views;
export import phoenix.control.controllers;

export namespace phoenix
{
struct ControllerViewFacts
{
	std::vector<std::string> systems;
	bool conversation_authority = false, contribution_authority = false, craft_authority = false,
	     transport_authority = false;
};
Json project_controller_runtime(const ConsoleControllerRuntime &, const ControllerViewFacts &,
                                std::span<const state_tree::Value> action_observations);
} // namespace phoenix
