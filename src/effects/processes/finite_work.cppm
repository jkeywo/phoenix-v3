module;
#include <algorithm>
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

export module phoenix.effects.finite_work;
export import phoenix.effects.funding;

export namespace phoenix
{
struct FiniteNativeWork
{
	CustomPropertyAddress owner;
	ProcessState frozen;
	std::int64_t work = 0;
	PreparedEffectGroup prepared;
	std::vector<RuleUpkeepPlan> payments;
	bool paid = false;
};
void validate_finite_work_commit(const FiniteNativeWork &, std::int64_t actual);
bool finite_run_current(const FiniteNativeWork &, const ProcessState *);
bool apply_finite_work_credits(const FiniteNativeWork &, ProcessState &);
bool record_finite_process_work(const FiniteNativeWork &, ProcessRuntime *, const ProcessDefinition &,
                                std::int64_t actual, std::int64_t progress, std::int64_t required, bool completed,
                                std::int64_t tick);
RuleUpkeepPlan finite_work_plan(const CustomPropertyAddress &, const ProcessState &, const NativeRuleContext &,
                                std::span<const EffectCostDefinition>, std::int64_t multiplier,
                                std::int64_t denominator, const std::vector<RuleWorkCredit> &, bool work);
} // namespace phoenix
