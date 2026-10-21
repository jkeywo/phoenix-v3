#pragma once
namespace phoenix
{
struct DisplacementDefinition
{
	std::string compartment;
	std::int64_t range = 0, capacity = 0, ticks = 0;
};
enum class AwayPhase
{
	Hidden,
	Available,
	Open,
	Outbound,
	Deployed,
	Inbound,
	Recovered
};
inline constexpr std::array away_phases{
    std::pair{AwayPhase::Hidden, "hidden"},      std::pair{AwayPhase::Available, "available"},
    std::pair{AwayPhase::Open, "open"},          std::pair{AwayPhase::Outbound, "outbound"},
    std::pair{AwayPhase::Deployed, "deployed"},  std::pair{AwayPhase::Inbound, "inbound"},
    std::pair{AwayPhase::Recovered, "recovered"}};
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
struct AwayMission
{
	AwayPhase phase = AwayPhase::Hidden;
	std::string transport, checkpoint, outcome, work_key, repair_compartment;
	std::map<std::string, std::string> assignments;
	std::vector<std::string> accompanying;
	std::uint64_t revision = 1, sequence = 0;
	std::int64_t transfer = 0, work = 0, progress = 0;
	// Remains true during a queued redeployment, until the next successful delivery.
	bool recovered = false;
	bool deployed() const
	{
		return phase == AwayPhase::Deployed || phase == AwayPhase::Inbound;
	}
	state_tree::Instance continuation;
	std::set<std::string> effects;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AwayMission, phase, transport, checkpoint, outcome, work_key, repair_compartment,
                                   assignments, accompanying, revision, sequence, transfer, work, progress, recovered,
                                   effects)
} // namespace phoenix
