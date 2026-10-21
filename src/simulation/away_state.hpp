#pragma once
namespace phoenix
{
struct DisplacementDefinition
{
	std::string compartment;
	std::int64_t range = 0, capacity = 0, ticks = 0;
};
struct AwayMission
{
	std::string phase = "hidden", transport, checkpoint, outcome, work_key, repair_compartment;
	std::map<std::string, std::string> assignments;
	std::vector<std::string> accompanying;
	std::uint64_t revision = 1, sequence = 0;
	std::int64_t transfer = 0, work = 0, progress = 0;
	bool deployed = false, recovered = false;
	state_tree::Instance continuation;
	std::set<std::string> effects;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AwayMission, phase, transport, checkpoint, outcome, work_key, assignments,
                                   accompanying, revision, sequence, transfer, work, progress, deployed, recovered,
                                   effects)
} // namespace phoenix
