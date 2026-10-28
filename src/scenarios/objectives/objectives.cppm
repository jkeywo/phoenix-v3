module;
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <string>
#include <vector>

export module phoenix.scenarios.objectives;
export import phoenix.scenario;

export namespace phoenix
{
struct PersonnelPosting
{
	EntityId target = 0;
	std::string status;
};
void to_json(Json &, const PersonnelPosting &);
void from_json(const Json &, PersonnelPosting &);
struct MissionTransition
{
	std::uint64_t id = 0, tick = 0;
	EntityId source = 0, subject = 0;
	std::string kind, operation, detail;
};
void to_json(Json &, const MissionTransition &);
void from_json(const Json &, MissionTransition &);
struct MissionLedger
{
	std::uint64_t next = 1, instance = 0;
	std::map<std::string, std::string> states;
	std::map<EntityId, std::set<std::string>> lost;
	std::vector<MissionTransition> transitions;
};
void to_json(Json &, const MissionLedger &);
void from_json(const Json &, MissionLedger &);

const char *objective_status_name(ObjectiveStatus);
MissionTransition append_mission_transition(MissionLedger &, std::uint64_t tick, std::uint64_t instance,
                                            std::size_t maximum_events, std::string kind, EntityId source,
                                            EntityId subject, std::string operation, std::string detail);
bool update_mission_state(MissionLedger &, const std::string &key, const std::string &value);
void validate_mission_history(const MissionLedger &, std::uint64_t tick, std::span<const EntityId> entities,
                              bool valid_instance);
struct MissionReportDetails
{
	Json ship_condition = Json::array(), crew = Json::array(), personnel = Json::array();
	std::map<std::string, std::string> asset_labels;
};
Json build_mission_report(const ScenarioRuntime &, const ScenarioReportDefinition &, ScenarioId, const ScenarioFacts &,
                          const MissionLedger &, EntityId, const MissionReportDetails &, Json legacy);
} // namespace phoenix
