module;
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

export module phoenix.gameplay.teams.routes;

export namespace phoenix
{
template <typename Links>
inline std::pair<std::int64_t, std::int64_t> compartment_route(std::size_t n, const Links &links, std::size_t from,
                                                               std::size_t to, std::int64_t *total_cost = nullptr)
{
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
		for (const auto &l : links)
		{
			const auto next = l.from == at ? l.to : l.to == at ? l.from : n;
			if (next == n || distance[next] <= distance[at] + l.ticks)
				continue;
			distance[next] = distance[at] + l.ticks;
			first[next] = at == from ? static_cast<std::int64_t>(next) : first[at];
			cost[next] = at == from ? l.ticks : cost[at];
		}
	}
	if (total_cost)
		*total_cost = distance.at(to);
	return {first.at(to), cost.at(to)};
}
} // namespace phoenix
