#pragma once
namespace phoenix
{
struct DisplacementDefinition
{
	std::string compartment;
	std::int64_t range = 0, capacity = 0, ticks = 0;
	std::string mode = "displacement", craft;
	std::int64_t count = 1, arrival_distance = 5000, arrival_speed = 1000, cruise_speed = 10000;
};
enum class AwayPhase
{
	Hidden,
	Available,
	Open,
	Outbound,
	Deployed,
	Inbound,
	Recovered,
	Lost
};
inline constexpr std::array away_phases{
    std::pair{AwayPhase::Hidden, "hidden"},       std::pair{AwayPhase::Available, "available"},
    std::pair{AwayPhase::Open, "open"},           std::pair{AwayPhase::Outbound, "outbound"},
    std::pair{AwayPhase::Deployed, "deployed"},   std::pair{AwayPhase::Inbound, "inbound"},
    std::pair{AwayPhase::Recovered, "recovered"}, std::pair{AwayPhase::Lost, "lost"}};
inline void to_json(Json &json, AwayPhase phase)
{
	for (const auto &[value, name] : away_phases)
		if (value == phase)
		{
			json = name;
			return;
		}
	throw std::runtime_error("Invalid away phase");
}
inline void from_json(const Json &json, AwayPhase &phase)
{
	for (const auto &[value, name] : away_phases)
		if (json == name)
		{
			phase = value;
			return;
		}
	throw std::runtime_error("Invalid saved away phase");
}
struct AwayCare
{
	std::int64_t location = 0, edge_to = -1, travel = 0;
	std::string order, activity;
	bool done = false, work_done = false;
	state_tree::Instance continuation;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AwayCare, location, edge_to, travel, order, activity, done, work_done)
struct BoardingState
{
	std::int64_t location = 0, destination = -1, edge_to = -1, travel = 0, assessment = 0, chance = 0, defenders = 0;
	std::string encounter;
	std::uint64_t assessment_message = 0;
	bool encounter_done = false, encounter_success = false, assessed = false, reported = false, captured = false,
	     prize_crew = false;
	std::map<std::string, bool> outcomes;
	std::map<std::string, std::uint64_t> neutralised;
	std::map<std::string, std::int64_t> factors;
	std::set<std::string> secured;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(BoardingState, location, destination, edge_to, travel, assessment, chance, defenders,
                                   encounter, encounter_done, encounter_success, assessed, reported, captured,
                                   prize_crew, outcomes, neutralised, factors, secured, assessment_message)
struct CarriedCraft
{
	EntityId id = 0, carrier = 0;
	std::string transport, phase = "stored", mission;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(CarriedCraft, id, carrier, transport, phase, mission)
struct AwayMission
{
	std::map<std::string, AwayCare> care;
	BoardingState boarding;
	EntityId craft = 0;
	AwayPhase phase = AwayPhase::Hidden;
	std::string transport, checkpoint, outcome, work_key, repair_compartment;
	std::map<std::string, std::string> assignments;
	std::vector<std::string> accompanying;
	std::uint64_t revision = 1, sequence = 0;
	std::int64_t transfer = 0, work = 0, progress = 0;
	// Remains true during a queued redeployment, until the next successful delivery.
	bool recovered = false, returning_without_delivery = false;
	bool ever_deployed = false;
	bool aboard_craft = false, transport_released = false;
	std::set<std::string> participants;
	bool deployed() const
	{
		return (phase == AwayPhase::Deployed || phase == AwayPhase::Inbound) && !aboard_craft &&
		       !returning_without_delivery;
	}
	bool absent() const
	{
		return deployed() || aboard_craft || (phase == AwayPhase::Outbound && craft);
	}
	state_tree::Instance continuation;
	std::set<std::string> effects;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AwayMission, phase, transport, checkpoint, outcome, work_key, repair_compartment,
                                   assignments, accompanying, revision, sequence, transfer, work, progress, recovered,
                                   effects, ever_deployed, participants, care, boarding, craft,
                                   returning_without_delivery, aboard_craft, transport_released)
} // namespace phoenix
