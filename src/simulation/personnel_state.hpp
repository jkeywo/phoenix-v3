#pragma once
namespace phoenix
{
struct OfficerState
{
	std::string duty;
	std::size_t location = 0;
	std::int64_t edge_to = -1, remaining = 0;
	bool present = false;
};
struct PersonnelState
{
	std::uint64_t revision = 1;
	std::vector<OfficerState> officers;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(OfficerState, duty, location, edge_to, remaining, present)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PersonnelState, revision, officers)
inline std::pair<std::int64_t, std::int64_t> personnel_route(const PersonnelDefinition &d, std::size_t from,
                                                             std::size_t to)
{
	const auto n = d.compartments.size();
	std::vector<std::int64_t> distance(n, INT64_MAX), first(n, -1), cost(n, 0);
	std::vector<bool> visited(n, false);
	distance.at(from) = 0;
	for (std::size_t step = 0; step < n; ++step)
	{
		std::size_t at = n;
		for (std::size_t i = 0; i < n; ++i)
			if (!visited[i] && (at == n || distance[i] < distance[at]))
				at = i;
		if (at == n || distance[at] == INT64_MAX)
			break;
		visited[at] = true;
		for (const auto &l : d.links)
		{
			const auto next = l.from == at ? l.to : l.to == at ? l.from : n;
			if (next == n || distance[next] <= distance[at] + l.ticks)
				continue;
			distance[next] = distance[at] + l.ticks;
			first[next] = at == from ? static_cast<std::int64_t>(next) : first[at];
			cost[next] = at == from ? l.ticks : cost[at];
		}
	}
	return {first.at(to), cost.at(to)};
}
inline PersonnelState initial_personnel(const PersonnelDefinition &d)
{
	PersonnelState state;
	for (const auto &o : d.officers)
		state.officers.push_back(
		    {o.duty,
		     static_cast<std::size_t>(std::find(d.compartments.begin(), d.compartments.end(), o.compartment) -
		                              d.compartments.begin()),
		     -1, 0, !o.duty.empty()});
	return state;
}
inline std::int64_t personnel_scale(std::int64_t value, std::int64_t bonus, bool inverse = false)
{
	const auto numerator = bonus >= 0 ? 1000 + bonus : 1000;
	const auto denominator = bonus >= 0 ? 1000 : 1000 - bonus;
	return inverse ? value * denominator / numerator : value * numerator / denominator;
}
} // namespace phoenix
