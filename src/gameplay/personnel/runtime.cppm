module;
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

export module phoenix.gameplay.personnel.runtime;
export import phoenix.gameplay.personnel.state;

export namespace phoenix
{
const PersonnelDuty *find_personnel_duty(const PersonnelDefinition &, const std::string &id);
std::int64_t personnel_assignment_score(const PersonnelDefinition &, const OfficerDefinition &, const OfficerState &,
                                        const PersonnelDuty &);

// Native immutable travel facts, collected after automatic duty assignment and
// before the personnel movement phase. Destinations follow authored duty order;
// traversal availability follows personnel order; links retain authored order.
struct PersonnelTravelFacts
{
	std::vector<OfficerState> destinations;
	std::vector<std::uint8_t> traversable;
	std::vector<PersonnelDefinition::Link> links;
};
void advance_personnel_movement(PersonnelState &, const PersonnelDefinition &, const PersonnelTravelFacts &,
                                std::uint64_t tick);
} // namespace phoenix
