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

export module phoenix.content.operations.types;
export import phoenix.data;
export import phoenix.contracts;
export import phoenix.state_tree;
export import phoenix.content.effects.types;

export namespace phoenix
{
struct PersonnelDefinition;
struct TeamOrderDefinition
{
	std::string id, label, icon, target, capability;
	std::set<std::string> patient_states{"minor", "severe", "stabilised"};
	state_tree::Program program;
};
struct AwaySlot
{
	std::string id, kind, trait;
	bool required = true;
	std::string team_type, capability;
	bool assign_ai = false;
};
struct BoardingEncounter
{
	std::string id, compartment, report;
	std::vector<std::string> defenders;
	std::map<std::string, std::int64_t> defences;
	std::int64_t success = 700, team_bonus = 100, defender_penalty = 150, injury = 250, loss = 50,
	             medical_reduction = 150, assessment_ticks = 20, officer_injury = 0, officer_loss = 0;
};
struct BoardingObjective
{
	std::string compartment, kind, system, fault;
	bool prize_crew = true;
	std::string prize_capability;
};
struct BoardingDefinition
{
	std::string id, title, dialogue, entry;
	bool retain_craft = true;
	state_tree::Program program;
	std::vector<AwaySlot> slots;
	std::map<std::string, std::size_t> compartments;
	std::map<std::string, BoardingEncounter> encounters;
	std::map<std::string, BoardingObjective> objectives;
};
struct AwayCraftSlot
{
	std::string id, role, craft_role;
	bool required = false;
};
struct SupplyOffer
{
	std::string provider_store, home_store, material, payment;
	std::int64_t limit = 0, give = 1, pay = 0;
};
struct AwayDefinition
{
	std::string id, title, slot, conversation, reveal_event, tree;
	// Empty retains the legacy unique/default Dispatch; explicit routes name fitted Systems.
	std::string dispatch;
	std::string hail_tree;
	std::vector<AwayCraftSlot> craft_slots;
	std::optional<SupplyOffer> supply;
	EntityId target = 0, origin = 0;
	bool retain_craft = true;
	std::optional<BoardingDefinition> boarding;
	std::vector<AwaySlot> slots;
	state_tree::Program program;
	std::map<std::string, std::size_t> repair_compartments;
	bool allow_ship_transport = false;
	std::vector<std::string> docking_ports;
	std::string ship_transport_reason;
	std::int64_t deployment_range = 20000, deployment_ticks = 20;
};
struct OfficerTrait
{
	std::string id, label;
	std::vector<ModifierEffectDefinition> effects;
	std::vector<RuleBinding> rules;
};
struct PersonnelDepartment
{
	std::string id, label, station;
};
struct PersonnelDuty
{
	std::string id, label, department, controller, team, compartment, console, required_trait, choir;
	std::map<std::string, std::vector<std::string>> effects;
};
struct OfficerDefinition
{
	std::string id, name, portrait, department, duty, compartment;
	std::string condition = "healthy";
	std::vector<std::string> traits, abilities;
};
struct MedicalFacility
{
	std::string id, compartment;
	std::int64_t capacity = 1, recovery_ticks = 1200;
};
struct CasualtyPolicy
{
	std::int64_t significant_damage = 20, heavy_below_permille = 300, lethal_damage = 80;
	std::int64_t stun = 150, minor = 100, severe = 30, killed = 10;
	bool lethal = false;
};
struct PersonnelDefinition
{
	std::vector<MedicalFacility> facilities;
	CasualtyPolicy casualties;
	std::vector<PersonnelDepartment> departments;
	std::map<std::string, OfficerTrait> traits;
	std::vector<PersonnelDuty> duties;
	std::map<std::string, std::size_t> duty_indices;
	std::vector<OfficerDefinition> officers;
	std::vector<std::string> compartments;
	struct Link
	{
		std::size_t from, to;
		std::int64_t ticks;
	};
	std::vector<Link> links;
};
} // namespace phoenix
