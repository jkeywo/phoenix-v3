module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>
export module phoenix.gameplay.psychic.state;
export import phoenix.world.state;
export import phoenix.state_tree;

export namespace phoenix
{

// Numeric continuation on fitted apparatus. No presentation or JSON work in the tick loop.
struct PsychicGhostShape
{
	Pose pose{};
	Motion motion{};
	std::int64_t acceleration_remainder = 0, turn_remainder = 0;
};
void to_json(Json &j, const PsychicGhostShape &g);
void from_json(const Json &j, PsychicGhostShape &g);
// Native projection state. AbilityRuntime owns activation, intent, targets and recovery.
struct PsychicContribution
{
	std::string grant, ability;
	std::int64_t aim_x = 0, aim_y = 0;
	std::uint64_t activation = 0;
	// Derived funded work; never restored as authority.
	std::int64_t work = 0;
	std::vector<PsychicGhostShape> shapes;
};
void to_json(Json &, const PsychicContribution &);
void from_json(const Json &, PsychicContribution &);
struct PsychicChoirState
{
	std::map<std::string, PsychicContribution> contributions;
};
void to_json(Json &, const PsychicChoirState &);
void from_json(const Json &, PsychicChoirState &);
struct GhostReception
{
	EntityId id = 0, observer = 0, source = 0;
	std::string choir, grant, power;
	std::size_t member = 0;
	std::uint64_t activation = 0, seen = 0;
	std::int64_t ended = -1;
	bool dispelled = false;
	PsychicGhostShape last;
};
void to_json(Json &, const GhostReception &);
void from_json(const Json &, GhostReception &);

} // namespace phoenix
