module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
export module phoenix.gameplay.away.state;
export import phoenix.data;
export import phoenix.contracts;
export import phoenix.state_tree;
export import phoenix.gameplay.boarding.state;

export namespace phoenix
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
	Recovered,
	Lost,
	Captured
};
inline constexpr std::array away_phases{
    std::pair{AwayPhase::Hidden, "hidden"},       std::pair{AwayPhase::Available, "available"},
    std::pair{AwayPhase::Open, "open"},           std::pair{AwayPhase::Outbound, "outbound"},
    std::pair{AwayPhase::Deployed, "deployed"},   std::pair{AwayPhase::Inbound, "inbound"},
    std::pair{AwayPhase::Recovered, "recovered"}, std::pair{AwayPhase::Lost, "lost"},
    std::pair{AwayPhase::Captured, "captured"}};

void to_json(Json &, AwayPhase);
void from_json(const Json &, AwayPhase &);
struct AwayCare
{
	std::int64_t location = 0, edge_to = -1, travel = 0;
	std::string order, activity;
	bool done = false, work_done = false;
	state_tree::Instance continuation;
};
void to_json(Json &, const AwayCare &);
void from_json(const Json &, AwayCare &);
struct AwayMission
{
	std::map<std::string, AwayCare> care;
	BoardingState boarding;
	AwayPhase phase = AwayPhase::Hidden;
	std::string transport, checkpoint, outcome, work_key, repair_compartment;
	std::map<std::string, std::string> assignments;
	std::map<std::string, EntityId> escorts;
	std::set<std::string> authorised_departments;
	std::int64_t supply_quantity = 0;
	std::uint64_t supply_job = 0;
	std::vector<std::string> accompanying;
	std::uint64_t revision = 1, sequence = 0;
	std::int64_t transfer = 0, work = 0, progress = 0, entry_compartment = 0;
	// Remains true during a queued redeployment, until the next successful delivery.
	bool recovered = false;
	bool ever_deployed = false;
	bool aboard_craft = false, transport_released = false;
	std::set<std::string> participants;
	bool deployed() const;
	bool absent() const;
	state_tree::Instance continuation;
	std::set<std::string> effects;
	std::set<std::string> flags;
	std::map<std::string, std::int64_t> saved_progress;
	bool timed_work = false;
	std::string work_capability;
	std::int64_t crew_deaths = 0;
};
void to_json(Json &, const AwayMission &);
void from_json(const Json &, AwayMission &);
} // namespace phoenix
