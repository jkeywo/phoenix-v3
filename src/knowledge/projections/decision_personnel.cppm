module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <vector>
export module phoenix.knowledge.decision_personnel;
export import phoenix.contracts;

export namespace phoenix
{
struct DecisionPatient
{
	std::string id, name, condition, facility, escort, source;
	std::int64_t location = 0, uncertainty = 0;
	bool stabilised = false;
	std::uint64_t observed_tick = 0, revision = 0;
	bool uncertainty_present = false;
	std::string compartment;
	std::uint64_t expires_tick = 0;
};
struct DecisionRepairNeed
{
	std::string system;
	std::int64_t compartment_index = 0;
	std::uint64_t observed_tick = 0;
	bool structural = false;
};
struct DecisionTeamRoute
{
	std::int64_t compartment = 0, ticks = 0;
	bool reachable = false;
};
struct DecisionFacility
{
	std::string id;
	std::int64_t compartment = 0, route_ticks = 0;
	bool available = false, reachable = false;
};
struct DecisionTeamOrder
{
	std::string id, label, icon, target;
	std::vector<std::int64_t> targets;
};
struct DecisionTeam
{
	std::string id, type, order, condition, activity, work_operation, work, work_system;
	std::uint64_t revision = 0, observed_tick = 0;
	std::int64_t location = 0, home = 0, destination = -1, travel = 0, assigned_compartment = -1;
	bool reserved = false, local_complete = false, available = true, work_done = false, departure_open = true;
	bool repair = false, completed = false, assignment_authority = false, manual = false, patient_control = false;
	bool routes_visible = false, facilities_visible = false;
	bool repair_work_active = false;
	std::vector<std::string> compartments;
	std::vector<DecisionTeamOrder> orders;
	std::vector<DecisionTeamRoute> routes;
	std::vector<DecisionFacility> facilities;
	std::vector<DecisionPatient> patients;
};
struct DecisionNamedLabel
{
	std::string id, label;
};
struct DecisionDutyEffect
{
	std::string system, slot, property;
	std::int64_t multiplier = 0;
};
struct DecisionDuty
{
	std::string id, label, department, location;
	bool assignment_reserved = false;
	std::vector<DecisionDutyEffect> effects;
};
struct DecisionTraitContribution
{
	std::string effect, name, role, property;
	std::int64_t delta_permille = 0;
};
struct DecisionOfficerTrait
{
	std::string id, label;
	std::vector<DecisionTraitContribution> contributions;
};
struct DecisionAssignmentRoute
{
	std::string duty;
	bool reachable = false, eligible = false;
	std::int64_t travel = 0;
};
struct DecisionOfficer
{
	std::string id, name, portrait, department, duty, condition, facility, location, destination, default_duty;
	bool present = false, stabilised = false, mobile = false, reserved = true;
	std::int64_t stun_until = 0, treatment = 0, remaining = 0;
	std::vector<DecisionOfficerTrait> traits;
	std::vector<DecisionAssignmentRoute> assignment_routes;
	std::vector<DecisionNamedLabel> abilities;
};
struct DecisionPersonnel
{
	bool present = false, manual = false;
	std::uint64_t revision = 0, generation = 0;
	std::string controller;
	std::vector<DecisionNamedLabel> departments;
	std::vector<DecisionDuty> duties;
	std::vector<DecisionOfficer> officers;
};
#define PHOENIX_DECISION_PERSONNEL_CODEC(T)                                                                            \
	void to_json(Json &, const T &);                                                                                   \
	void from_json(const Json &, T &);
PHOENIX_DECISION_PERSONNEL_CODEC(DecisionPatient)
PHOENIX_DECISION_PERSONNEL_CODEC(DecisionRepairNeed)
PHOENIX_DECISION_PERSONNEL_CODEC(DecisionTeamRoute)
PHOENIX_DECISION_PERSONNEL_CODEC(DecisionFacility)
PHOENIX_DECISION_PERSONNEL_CODEC(DecisionTeamOrder)
PHOENIX_DECISION_PERSONNEL_CODEC(DecisionTeam)
PHOENIX_DECISION_PERSONNEL_CODEC(DecisionNamedLabel)
PHOENIX_DECISION_PERSONNEL_CODEC(DecisionDutyEffect)
PHOENIX_DECISION_PERSONNEL_CODEC(DecisionDuty)
PHOENIX_DECISION_PERSONNEL_CODEC(DecisionTraitContribution)
PHOENIX_DECISION_PERSONNEL_CODEC(DecisionOfficerTrait)
PHOENIX_DECISION_PERSONNEL_CODEC(DecisionAssignmentRoute)
PHOENIX_DECISION_PERSONNEL_CODEC(DecisionOfficer)
PHOENIX_DECISION_PERSONNEL_CODEC(DecisionPersonnel)
#undef PHOENIX_DECISION_PERSONNEL_CODEC
Json decision_teams_json(const std::vector<DecisionTeam> &);
Json decision_personnel_json(const DecisionPersonnel &);
} // namespace phoenix
