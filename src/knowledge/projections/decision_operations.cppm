module;
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

export module phoenix.knowledge.decision_operations;
export import phoenix.contracts;

export namespace phoenix
{
struct AwaySlotDecision
{
	std::string id, role, assigned;
	bool required = false, owned = false, lost = false, craft = false;
	std::vector<std::string> choices;
};
struct AwaySupplyDecision
{
	std::string material, payment, phase;
	std::int64_t give = 0, pay = 0, quantity = 0, payment_quantity = 0, delivered = 0;
	bool manual = false;
	std::vector<std::int64_t> quantities;
};
struct AwayBoardingDecision
{
	std::int64_t chance_permille = 0, defenders = 0;
	std::string encounter;
	std::map<std::string, std::int64_t> factors;
};
struct AwayPeerDecision
{
	std::string console, controller;
	std::uint64_t id = 0;
};
struct AwayDepartmentDecision
{
	std::int64_t assigned = 0;
	bool ready = false, contributing = false, transport_released = false, recall_requested = false,
	     launch_ready = false;
	std::uint64_t observed_tick = 0, revision = 0;
	std::vector<AwayPeerDecision> peers;
	std::map<std::string, std::string> transport_systems;
};
struct AwayCarePatientDecision
{
	std::string id, condition;
	bool stabilised = false;
};
struct AwayCareDecision
{
	std::string team, controller;
	std::int64_t location = 0, destination = -1, travel = 0;
	bool local_complete = false;
	std::uint64_t observed_tick = 0;
	std::vector<std::string> compartments;
	std::vector<AwayCarePatientDecision> patients;
};
struct AwayMissionDecision
{
	std::string id, title, phase, result, transport, owner, dispatch_system, blocker, ship_transport_reason,
	    ship_support, launch_blocker;
	std::uint64_t revision = 0;
	EntityId target = 0;
	bool dispatch = false, ai_dispatch = false, automatic_dispatch = false, can_contribute = false,
	     contributing = false, transport_owned = false, launch_ready = false, work_complete = false;
	std::vector<AwaySlotDecision> slots, craft_slots;
	std::vector<std::string> transports, accompanying, docking_ports;
	std::map<std::string, std::string> labels, portraits, crew_names;
	std::optional<AwaySupplyDecision> supply;
	std::optional<AwayBoardingDecision> boarding_check;
	std::optional<AwayDepartmentDecision> department;
	std::vector<AwayCareDecision> care;
	bool care_reported = false;
};
struct CraftDecisionRow
{
	EntityId entity = 0, target = 0;
	std::string id, bay, role, phase, order, pending_order, mission, blocker;
	std::uint64_t revision = 0, report_tick = 0;
	bool linked = false, pending = false, reserved = false, ftl_preparing = false, available = false,
	     recovery_available = false, recall_active = false, armed = false, manual = false;
	std::int64_t return_reserve_permille = 200, transport_capacity = 0, energy = 0, capacity = 0, ammunition = 0,
	             condition = 0, maximum_condition = 0;
};
struct CraftGuidanceDecision
{
	std::int64_t x = 0, y = 0, altitude = 0, speed = 0, heading = 0;
	EntityId target = 0;
	bool navigate = false;
};
struct CraftSelfDecision
{
	std::string order;
	EntityId target = 0;
	bool reserve_low = false, armed = false;
	std::int64_t anchor_x = 0, anchor_y = 0, engagement_range = 0, pursuit_altitude_offset = 0;
	std::optional<CraftGuidanceDecision> guidance;
};

// Explicit presentation/provider serialization boundaries. Ordinary decisions
// consume the records above directly and never reconstruct them from JSON.
void to_json(Json &, const AwaySlotDecision &);
void to_json(Json &, const AwaySupplyDecision &);
void to_json(Json &, const AwayBoardingDecision &);
void to_json(Json &, const AwayPeerDecision &);
void to_json(Json &, const AwayDepartmentDecision &);
void to_json(Json &, const AwayCarePatientDecision &);
void to_json(Json &, const AwayCareDecision &);
void to_json(Json &, const AwayMissionDecision &);
void to_json(Json &, const CraftDecisionRow &);
void to_json(Json &, const CraftGuidanceDecision &);
void to_json(Json &, const CraftSelfDecision &);
void from_json(const Json &, AwaySlotDecision &);
void from_json(const Json &, AwaySupplyDecision &);
void from_json(const Json &, AwayBoardingDecision &);
void from_json(const Json &, AwayPeerDecision &);
void from_json(const Json &, AwayDepartmentDecision &);
void from_json(const Json &, AwayCarePatientDecision &);
void from_json(const Json &, AwayCareDecision &);
void from_json(const Json &, AwayMissionDecision &);
void from_json(const Json &, CraftDecisionRow &);
void from_json(const Json &, CraftGuidanceDecision &);
void from_json(const Json &, CraftSelfDecision &);
} // namespace phoenix
