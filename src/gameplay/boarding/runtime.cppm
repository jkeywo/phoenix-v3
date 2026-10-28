module;
#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

export module phoenix.gameplay.boarding.runtime;
export import phoenix.gameplay.boarding.state;
export import phoenix.gameplay.combat.state;
export import phoenix.gameplay.personnel.state;

export namespace phoenix
{
struct BoardingAssessmentFacts
{
	std::int64_t security_strength = 0;
	std::vector<std::int64_t> defender_penalties;
	std::vector<std::pair<std::string, std::int64_t>> defence_effects;
};
// Returns whether report-visible factors changed. Report transport remains with
// conversations; the encounter owns its chance, assessment clock and report state.
bool assess_boarding(BoardingState &, const BoardingEncounter &, const BoardingAssessmentFacts &);
bool advance_boarding_route(BoardingState &, std::size_t compartments,
                            std::span<const PersonnelDefinition::Link> open_links, bool traversable);
bool boarding_route_required_after_tick(const BoardingState &, bool traversable);
std::int64_t boarding_random_roll(std::uint64_t &random);

// Bounded mutable casualty targets. The operation preserves shared RNG draw order
// and creates loss records only when a team or accompanying officer is lost.
struct BoardingCasualtyActors
{
	EntityId ship;
	const std::string &team;
	TeamState &team_state;
	const PersonnelDefinition &definition;
	PersonnelState &personnel;
	std::uint64_t &random;
	std::map<EntityId, std::set<std::string>> &lost;
};
void apply_boarding_casualties(BoardingCasualtyActors, const BoardingEncounter &, bool medical);
} // namespace phoenix
