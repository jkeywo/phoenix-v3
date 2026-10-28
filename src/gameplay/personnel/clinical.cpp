module;
#include <algorithm>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

module phoenix.gameplay.personnel.clinical;

namespace phoenix
{
bool clinical_patient_eligible(const OfficerState &o)
{
	return (o.condition == "minor" || o.condition == "severe") && o.facility.empty() && o.escort.empty();
}
bool clinical_patient_matches(const OfficerState &o, const TeamOrderDefinition &order, const std::string &activity,
                              bool with_party, bool away)
{
	auto candidate = o;
	if (with_party && candidate.facility == "away")
		candidate.facility.clear();
	if (!clinical_patient_eligible(candidate) ||
	    !order.patient_states.contains(o.condition == "severe" && o.stabilised ? "stabilised" : o.condition) ||
	    (!with_party && away))
		return false;
	if (activity == "recover")
		return o.condition == "severe" && o.stabilised;
	if (activity == "stabilise")
		return o.condition == "severe" && !o.stabilised;
	if (activity == "treat")
		return o.condition == "minor" || (o.condition == "severe" && !o.stabilised);
	return true;
}
ClinicalRoster::ClinicalRoster(const PersonnelDefinition &definition, const PersonnelState &state)
{
	for (std::size_t i = 0; i < definition.officers.size(); ++i)
		patients_.push_back({definition.officers[i].id, definition.officers[i].name, {}, state.officers[i]});
}
void ClinicalRoster::officer_away(std::size_t index, bool away)
{
	patients_.at(index).away = away;
}
void ClinicalRoster::append_team(const std::string &id, std::size_t location, const std::string &condition, bool away)
{
	OfficerState state;
	state.location = location;
	state.condition = condition == "injured" ? "minor" : condition == "lost" ? "dead" : "healthy";
	if (away)
		state.facility = "away";
	patients_.push_back({"team:" + id, id, id, state});
}
const std::vector<ClinicalPatient> &ClinicalRoster::patients() const
{
	return patients_;
}
std::vector<ClinicalPatient> ClinicalRoster::take_patients() &&
{
	return std::move(patients_);
}
std::optional<std::size_t> ClinicalRoster::select(EntityId target, std::optional<std::size_t> compartment,
                                                  const TeamOrderDefinition &order, const std::string &activity,
                                                  const std::string &priority,
                                                  const std::set<std::pair<EntityId, std::size_t>> &claimed,
                                                  const std::set<std::size_t> *party) const
{
	std::optional<std::size_t> best;
	std::tuple<int, int, std::string> score;
	for (std::size_t i = 0; i < patients_.size(); ++i)
	{
		const auto &o = patients_[i].state;
		if ((compartment && o.location != *compartment) ||
		    !clinical_patient_matches(o, order, activity, party != nullptr, patients_[i].away) ||
		    (party && !party->contains(i)) || claimed.contains({target, i}))
			continue;
		const auto candidate = std::tuple{o.condition == "severe" && !o.stabilised ? 3
		                                  : o.condition == "minor"                 ? 2
		                                                                           : 1,
		                                  patients_[i].id == priority ? 1 : 0, patients_[i].id};
		if (!best || candidate > score)
		{
			best = i;
			score = candidate;
		}
	}
	return best;
}
} // namespace phoenix
