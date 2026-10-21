#pragma once
namespace phoenix
{
enum class CraftPhase
{
	Docked,
	Launching,
	Flying,
	Recovering,
	Lost
};
NLOHMANN_JSON_SERIALIZE_ENUM(CraftPhase, {{CraftPhase::Docked, "docked"},
                                          {CraftPhase::Launching, "launching"},
                                          {CraftPhase::Flying, "flying"},
                                          {CraftPhase::Recovering, "recovering"},
                                          {CraftPhase::Lost, "lost"}})
struct CraftReport
{
	CraftPhase phase = CraftPhase::Docked;
	std::string order = "idle";
	EntityId target = 0;
	std::uint64_t tick = 0;
	std::int64_t energy = 0, capacity = 0, ammunition = 0, condition = 0, maximum_condition = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CraftReport, phase, order, target, tick, energy, capacity, ammunition, condition,
                                   maximum_condition)
struct CraftState
{
	EntityId home = 0, target = 0;
	std::string bay, order = "idle", pending_order, mission;
	EntityId pending_target = 0;
	std::size_t slot = 0;
	CraftPhase phase = CraftPhase::Docked;
	std::uint64_t revision = 1;
	std::int64_t progress = 0;
	bool ftl_recalled = false;
	CraftReport report;
	fixed::Vec rendezvous, rendezvous_velocity, target_position, target_velocity;
	std::int64_t rendezvous_altitude = 0, target_altitude = 0;
	fixed::Vec pending_position, pending_velocity;
	std::int64_t pending_altitude = 0;
};
struct CraftGuidance
{
	fixed::Vec destination;
	std::int64_t altitude = 0, speed = 0, heading = 0;
	EntityId target = 0;
	bool navigate = false;
};
struct CraftBayState
{
	std::int64_t energy_remainder = 0;
	std::vector<std::int64_t> ammunition_remainders;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CraftBayState, energy_remainder, ammunition_remainders)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(VerticalMotion, altitude, velocity, remainder, acceleration_remainder, desired)
} // namespace phoenix
