module;
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <vector>

export module phoenix.control.objectives;

export namespace phoenix
{
enum class ObjectiveTier : std::uint8_t
{
	Reaction,
	ScriptedHigh,
	ObjectiveCombat,
	DirectiveCombat,
	ScriptedLow,
	ObjectiveOther,
	DirectiveOther
};
// All factors are bounded estimates from permitted evidence, never World queries.
struct ObjectiveFactors
{
	std::int64_t urgency = 0, progress = 0, risk = 0, travel = 0, resources = 0;
};
struct ObjectiveScoringPolicy
{
	std::int64_t urgency = 1, progress = 1, risk = 1, travel = 1, resources = 1, commitment = 100;
};
struct ObjectiveCandidate
{
	std::uint64_t objective = 0, target = 0;
	std::string action;
	ObjectiveTier tier = ObjectiveTier::ObjectiveOther;
	bool marked = false, feasible = false, committed = false;
	ObjectiveFactors factors;
	ObjectiveScoringPolicy policy;
	std::string reason;
};
struct ObjectiveScore
{
	std::array<std::int64_t, 6> terms{};
	std::int64_t total = 0;
};
ObjectiveScore score_objective(const ObjectiveCandidate &);
std::optional<std::size_t> select_objective(std::span<const ObjectiveCandidate>);
ObjectiveTier objective_tier(const std::string &);
const char *objective_tier_name(ObjectiveTier);
} // namespace phoenix
