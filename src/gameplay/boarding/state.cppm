module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
export module phoenix.gameplay.boarding.state;
export import phoenix.data;

export namespace phoenix
{
struct BoardingState
{
	std::int64_t location = 0, destination = -1, edge_to = -1, travel = 0, assessment = 0, chance = 0, defenders = 0;
	std::string encounter;
	std::uint64_t assessment_message = 0;
	bool encounter_done = false, encounter_success = false, assessed = false, reported = false, captured = false,
	     prize_crew = false;
	std::map<std::string, bool> outcomes;
	std::map<std::string, std::uint64_t> neutralised;
	std::map<std::string, std::int64_t> factors;
	std::set<std::string> secured;
};
void to_json(Json &, const BoardingState &);
void from_json(const Json &, BoardingState &);
} // namespace phoenix
