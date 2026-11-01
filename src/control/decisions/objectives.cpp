module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

module phoenix.control.objectives;

namespace phoenix
{
namespace
{
constexpr std::array<const char *, 7> tier_names{"reaction",         "scripted_high", "objective_combat",
                                                 "directive_combat", "scripted_low",  "objective_other",
                                                 "directive_other"};
int objective_pass(const ObjectiveCandidate &candidate)
{
	return candidate.tier == ObjectiveTier::Reaction ? 0 : candidate.marked ? 1 : 2;
}
} // namespace
ObjectiveTier objective_tier(const std::string &name)
{
	const auto at = std::find(tier_names.begin(), tier_names.end(), name);
	if (at == tier_names.end())
		throw std::runtime_error("Unknown objective tier: " + name);
	return static_cast<ObjectiveTier>(at - tier_names.begin());
}
const char *objective_tier_name(ObjectiveTier tier)
{
	return tier_names.at(static_cast<std::size_t>(tier));
}
ObjectiveScore score_objective(const ObjectiveCandidate &candidate)
{
	// Compile-time validation bounds weights; clamp evidence estimates as well,
	// so imported reports cannot overflow deterministic integer arithmetic.
	const auto term = [](std::int64_t factor, std::int64_t weight)
	{
		return std::clamp(factor, std::int64_t{0}, std::int64_t{1000000}) *
		       std::clamp(weight, std::int64_t{0}, std::int64_t{1000000});
	};
	const auto &f = candidate.factors;
	const auto &p = candidate.policy;
	ObjectiveScore result;
	result.terms = {term(f.urgency, p.urgency),      term(f.progress, p.progress),
	                -term(f.risk, p.risk),           -term(f.travel, p.travel),
	                -term(f.resources, p.resources), candidate.committed ? term(1, p.commitment) : 0};
	for (const auto value : result.terms)
		result.total += value;
	return result;
}
std::optional<std::size_t> select_objective(std::span<const ObjectiveCandidate> candidates)
{
	std::optional<std::size_t> best;
	const auto key = [](const ObjectiveCandidate &candidate)
	{
		return std::tuple{objective_pass(candidate), candidate.tier,   -score_objective(candidate).total,
		                  candidate.objective,       candidate.target, candidate.action};
	};
	for (std::size_t i = 0; i < candidates.size(); ++i)
		if (candidates[i].feasible && (!best || key(candidates[i]) < key(candidates[*best])))
			best = i;
	return best;
}
} // namespace phoenix
