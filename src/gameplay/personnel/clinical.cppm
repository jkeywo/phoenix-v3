module;
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

export module phoenix.gameplay.personnel.clinical;
export import phoenix.gameplay.personnel.state;

export namespace phoenix
{
struct ClinicalPatient
{
	std::string id, name, team;
	OfficerState state;
	bool away = false;
};
bool clinical_patient_eligible(const OfficerState &);
bool clinical_patient_matches(const OfficerState &, const TeamOrderDefinition &, const std::string &activity,
                              bool with_party, bool away);

// Owns the ordered patient projection used for triage. World supplies located
// personnel/team facts; eligibility, clinical priorities and tie-breaking live here.
class ClinicalRoster
{
public:
	ClinicalRoster(const PersonnelDefinition &, const PersonnelState &);
	void officer_away(std::size_t index, bool away);
	void append_team(const std::string &id, std::size_t location, const std::string &condition, bool away);
	const std::vector<ClinicalPatient> &patients() const;
	std::vector<ClinicalPatient> take_patients() &&;
	std::optional<std::size_t> select(EntityId target, std::optional<std::size_t> compartment,
	                                  const TeamOrderDefinition &, const std::string &activity,
	                                  const std::string &priority,
	                                  const std::set<std::pair<EntityId, std::size_t>> &claimed,
	                                  const std::set<std::size_t> *party) const;

private:
	std::vector<ClinicalPatient> patients_;
};
} // namespace phoenix
