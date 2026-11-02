module;
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>
export module phoenix.knowledge.decision;
export import phoenix.knowledge.decision_machinery;
export import phoenix.knowledge.decision_combat;
export import phoenix.knowledge.decision_personnel;
export import phoenix.knowledge.decision_travel;
export import phoenix.knowledge.decision_operations;
export import phoenix.knowledge.decision_psychic;
export namespace phoenix
{
struct DecisionAuthority
{
	std::set<KnowledgeId> priority_objectives;
	std::uint64_t priority_revision = 0;
	std::string alert, stance;
	std::int64_t alert_severity = 0, alert_maximum = 0;
};
// Frozen operational inputs. Presentation JSON is encoded independently.
struct DecisionObservation
{
	BodyView own;
	std::vector<BodyView> contacts;
	std::vector<BearingContactView> bearing_contacts;
	std::int64_t requested_speed = 0, requested_heading = 0;
	bool engine_enabled = true, radar_enabled = true;
	std::string console;
	SystemMask controls = 7, feeds = 7;
	std::vector<DecisionSystemView> systems;
	std::uint64_t observed_tick = 0;
	DecisionAuthority authority;
	CombatDecisionSnapshot combat;
	SensorDecisionSnapshot sensors;
	MachineryDecisionSnapshot machinery;
	std::vector<DecisionTeam> teams;
	DecisionPersonnel personnel;
	std::vector<DecisionPatient> reported_patients;
	std::vector<DecisionRepairNeed> repair_needs;
	DockingDecisionSnapshot docking;
	LogisticsDecisionSnapshot logistics;
	std::vector<FtlDecisionRow> ftl;
	std::vector<AwayMissionDecision> away_missions;
	std::vector<CraftDecisionRow> craft;
	std::optional<CraftSelfDecision> craft_self;
	PsychicDecisionSnapshot psychic;
};
} // namespace phoenix
