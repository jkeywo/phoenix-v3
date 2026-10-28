module;
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

export module phoenix.control.coordination_facts;
export import phoenix.control.controllers;

export namespace phoenix
{
struct ControllerCoordinationInputs
{
	std::vector<std::string> systems;
	bool navigation_blocked = false, hold = false;
};
void controller_coordination_facts(const ConsoleControllerRuntime &, const Observation &, KnowledgeTick,
                                   const ControllerCoordinationInputs &, std::vector<state_tree::Value> &);
} // namespace phoenix
