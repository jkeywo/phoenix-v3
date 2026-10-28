module;
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

export module phoenix.gameplay.personnel.state;
export import phoenix.content.operations.types;
export import phoenix.effects.contracts;
export import phoenix.effects.processes;

export namespace phoenix
{
struct OfficerState
{
	std::string duty;
	std::size_t location = 0;
	std::int64_t edge_to = -1, remaining = 0;
	bool present = false, detained = false;
	std::string condition = "healthy", facility, escort;
	bool stabilised = false;
	std::int64_t stun_until = 0, treatment = 0, recovery = 0, casualty_tick = -1;
	CustomPropertyState properties;
	ProcessRuntime processes;
};
struct PersonnelState
{
	std::uint64_t revision = 1;
	std::vector<OfficerState> officers;
};
void to_json(Json &, const OfficerState &);
void from_json(const Json &, OfficerState &);
inline bool officer_healthy(const OfficerState &o)
{
	return !o.detained && o.condition == "healthy";
}
inline bool officer_mobile(const OfficerState &o)
{
	return !o.detained && (o.condition == "healthy" || o.condition == "minor");
}
void to_json(Json &, const PersonnelState &);
void from_json(const Json &, PersonnelState &);
inline PersonnelState initial_personnel(const PersonnelDefinition &d)
{
	PersonnelState state;
	for (const auto &o : d.officers)
		state.officers.push_back(
		    {o.duty,
		     static_cast<std::size_t>(std::find(d.compartments.begin(), d.compartments.end(), o.compartment) -
		                              d.compartments.begin()),
		     -1, 0, !o.duty.empty()});
	for (std::size_t i = 0; i < d.officers.size(); ++i)
	{
		state.officers[i].condition = d.officers[i].condition;
		if (!officer_mobile(state.officers[i]))
			state.officers[i].present = false;
	}
	return state;
}
} // namespace phoenix
