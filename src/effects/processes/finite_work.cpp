module;
#include <algorithm>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.effects.finite_work;

namespace phoenix
{
void validate_finite_work_commit(const FiniteNativeWork &quote, std::int64_t actual)
{
	if (!quote.paid || actual < 0 || actual > quote.work)
		throw std::runtime_error("Invalid committed finite native work");
}
bool finite_run_current(const FiniteNativeWork &quote, const ProcessState *state)
{
	return state && state->generation == quote.frozen.generation && state->run == quote.frozen.run && state->admitted &&
	       state->requested;
}
bool apply_finite_work_credits(const FiniteNativeWork &quote, ProcessState &state)
{
	if (!finite_run_current(quote, &state))
		return false;
	for (const auto &plan : quote.payments)
	{
		auto &credits = plan.process_work ? state.work_credits : state.idle_credits;
		apply_rule_work_credits(credits, plan);
	}
	return true;
}
bool record_finite_process_work(const FiniteNativeWork &quote, ProcessRuntime *runtime,
                                const ProcessDefinition &definition, std::int64_t actual, std::int64_t progress,
                                std::int64_t required, bool completed, std::int64_t tick)
{
	validate_finite_work_commit(quote, actual);
	ProcessState *state = nullptr;
	if (runtime)
	{
		const auto found = runtime->processes.find(quote.frozen.id);
		if (found != runtime->processes.end())
			state = &found->second;
	}
	const bool active = finite_run_current(quote, state);
	if (active)
	{
		state->progress = std::clamp(progress, std::int64_t{0}, std::max(required, progress));
		state->required_work = required;
		state->ready = completed;
		state->work_output = actual ? 1000 : 0;
		state->actual_work += actual;
		state->last_work = tick;
	}
	if (!runtime)
		return false;
	if (actual)
	{
		auto receipt = std::find_if(runtime->work.begin(), runtime->work.end(),
		                            [&](const auto &entry)
		                            {
			                            return entry.execution == quote.frozen.id &&
			                                   entry.generation == quote.frozen.generation &&
			                                   entry.activation == quote.frozen.run;
		                            });
		if (receipt == runtime->work.end())
			runtime->work.push_back({process_owner_kind(quote.owner), process_owner_name(quote.owner),
			                         definition.family, definition.id, quote.frozen.run, 1000, quote.frozen.id, actual,
			                         definition.work_unit, quote.frozen.generation});
		else
			receipt->work += actual;
	}
	runtime->work_tick = tick;
	return completed && active;
}
RuleUpkeepPlan finite_work_plan(const CustomPropertyAddress &owner, const ProcessState &frozen,
                                const NativeRuleContext &context, std::span<const EffectCostDefinition> costs,
                                std::int64_t multiplier, std::int64_t denominator,
                                const std::vector<RuleWorkCredit> &credits, bool work)
{
	RuleUpkeepPlan plan;
	plan.context = context;
	plan.costs.assign(costs.begin(), costs.end());
	for (auto &cost : plan.costs)
		cost.amount = process_cost_product(cost.amount, multiplier);
	plan.unit_work = denominator;
	plan.credits = credits;
	plan.process_owner = owner;
	plan.process = frozen.id;
	plan.process_work = work;
	return plan;
}

} // namespace phoenix
