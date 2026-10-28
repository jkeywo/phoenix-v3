module;
#include <set>
#include <cstdint>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

export module phoenix.content.sensing.types;
export import phoenix.data;
export import phoenix.contracts;
export import phoenix.state_tree;
export import phoenix.content.effects.types;

export namespace phoenix
{
struct SensorDefinition
{
	std::int64_t long_range = 0;
	std::int64_t arc = 360000, bearing = 0;
	std::string turret;
	std::map<std::string, std::int64_t> capabilities;
	std::map<std::string, std::int64_t> passive_capabilities;
};
struct SensorFinding
{
	std::string id, text, requires_event, unless_event, event;
	std::string tag, destination_station;
	EntityId target = 0;
	std::vector<std::string> capabilities;
	std::int64_t ticks = 1;
};
struct InterdictionShip
{
	std::string definition;
	std::int64_t x = 0, y = 0, heading = 0, formation_bearing = 0;
};
struct InterdictionDefinition
{
	std::string id, slot, start_event, tree, dialogue, target_character;
	EntityId target = 0;
	std::int64_t threat = 0, clearance = 0, engagement = 0, formation = 0, takeover_ticks = 0;
	std::int64_t combat_standoff = 10000, cruise_speed = 20000;
	std::map<std::string, std::vector<InterdictionShip>> variants;
	std::vector<std::pair<std::int64_t, std::string>> pleas;
	std::vector<std::string> occupation_teams;
	// Omitted effects retain the historical takeover contract for embedded saves.
	bool repair_on_takeover = true, kill_original_crew = true, capture_visiting_personnel = true;
	state_tree::Program program;
};
struct ChoirSeatDefinition
{
	std::string duty, ability, control;
	std::size_t controller = 0, enabled = 0, target = 0;
	std::optional<std::size_t> planner;
	std::size_t planning_enabled = 0, planning_target = 0;
};
struct ChoirDefinition
{
	std::string id, compartment, planning_controller;
	std::set<std::string> powers;
	std::int64_t range = 500000, arc = 90000;
	std::vector<ChoirSeatDefinition> seats;
};
struct PsychicDefinition
{
	SharedAbilityCatalog catalog;
	std::map<std::string, ChoirDefinition> choirs;
	bool has_power(const std::string &ability) const;
	const PsychicPowerDefinition &power(const std::string &ability) const;
};
} // namespace phoenix
