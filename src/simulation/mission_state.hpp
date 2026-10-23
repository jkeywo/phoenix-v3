#pragma once
namespace phoenix
{
struct PersonnelPosting
{
	EntityId target = 0;
	std::string status;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PersonnelPosting, target, status)
struct MissionTransition
{
	std::uint64_t id = 0, tick = 0;
	EntityId source = 0, subject = 0;
	std::string kind, operation, detail;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MissionTransition, id, tick, source, subject, kind, operation, detail)
struct MissionLedger
{
	std::uint64_t next = 1, instance = 0;
	std::map<std::string, std::string> states;
	std::map<EntityId, std::set<std::string>> lost;
	std::vector<MissionTransition> transitions;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(MissionLedger, next, instance, states, lost, transitions)
} // namespace phoenix
