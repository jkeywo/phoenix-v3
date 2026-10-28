module;
#include "native_headers.hpp"

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::prepare_process_funding_effects(std::span<const RuleProcessWork> completed,
                                              std::map<EntityId, std::optional<ShipResourceAllocation>> &allocations)
    -> bool
{
	// Each failed mandatory exit is excluded once from this prepared batch.
	// No native work or world effect executes during these bounded requotes.
	for (;;)
	{
		if (!rule_upkeep_effects)
			throw std::runtime_error("Process allocation quote unavailable");
		rule_upkeep_effects->defer_damage = true;
		std::vector<EffectGroupInvocation> frames;
		std::vector<std::pair<CustomPropertyAddress, std::string>> owners;
		std::vector<bool> exits;
		for (const auto &[key, plan] : rule_upkeep_plans)
		{
			if (!plan.process_owner || plan.process_work)
				continue;
			const auto &address = *plan.process_owner;
			const auto *state = process_state(address, plan.process);
			if (!state)
				continue;
			const auto receipt = std::find_if(completed.begin(), completed.end(),
			                                  [&](const auto &value)
			                                  {
				                                  return value.owner == address && value.id == plan.process;
			                                  });
			const auto idle = process_funding_output(address, plan.process, false);
			const auto funding = process_funding_output(address, plan.process, true);
			const auto activity = receipt == completed.end() ? idle : receipt->activity;
			const auto work = receipt == completed.end() ? 0 : receipt->work;
			auto candidate = *state;
			const auto edge = stage_process_funding_state(address, candidate, idle, funding);
			const auto exit_key = std::pair{address, process_run_source(address, *state)};
			const auto failed_exit = process_exit_failures.find(exit_key);
			if (const auto lifecycle = process_funding_effect(address, *state, idle, funding))
			{
				const bool exit = edge == "pause" || edge == "stop";
				if (exit && failed_exit != process_exit_failures.end())
				{
					const auto diagnostic = "process exit " + exit_key.second + ": " + failed_exit->second;
					if (std::find(rule_upkeep_effects->effect_failures.begin(),
					              rule_upkeep_effects->effect_failures.end(),
					              diagnostic) == rule_upkeep_effects->effect_failures.end())
						rule_upkeep_effects->effect_failures.push_back(diagnostic);
				}
				else
				{
					auto effects = *lifecycle;
					if ((plan.producer && edge == "resume" && plan.resume_fees) ||
					    (exit && rule_upkeep_effects->prepaid_process_exits.contains(exit_key)))
					{
						effects.costs.clear();
						effects.energy = 0;
					}
					frames.push_back(process_effect_invocation(address, *state, effects));
					owners.emplace_back(address, plan.process);
					exits.push_back(exit);
				}
				if (edge == "stop")
					rule_upkeep_effects->process_stops.insert(exit_key);
			}
			if (work > 0)
			{
				const auto evaluation = process_work_effects(address, *state, work, activity);
				if (evaluation.status != RuleEvaluationStatus::Ready)
				{
					reject_process_funding(address, plan.process, "Invalid process work effect or arithmetic");
					return true;
				}
				auto effects = evaluation.quote.effects;
				effects.costs.clear();
				effects.energy = 0;
				frames.push_back(process_effect_invocation(address, *state, effects));
				owners.emplace_back(address, plan.process);
				exits.push_back(false);
			}
			stage_rule_upkeep_process_receipt(address, plan.process, work, idle, activity);
			if (failed_exit != process_exit_failures.end())
				prepare_process_owner(*rule_upkeep_effects, address).processes.at(plan.process).blocker =
				    "Process exit effect failed";
		}
		std::size_t failed = static_cast<std::size_t>(-1);
		const auto failure = append_rule_upkeep_effects(frames, failed);
		if (failure.empty())
			return false;
		const auto root = failed < rule_upkeep_effects->invocation_roots.size()
		                      ? rule_upkeep_effects->invocation_roots.at(failed)
		                      : failed;
		if (root >= owners.size())
			throw std::runtime_error("Process work batch failed without an invocation: " + failure);
		const auto &[address, id] = owners[root];
		if (!exits[root])
		{
			reject_process_funding(address, id, failure);
			return true;
		}
		const auto *state = process_state(address, id);
		if (!state)
			return true;
		process_exit_failures.emplace(std::pair{address, process_run_source(address, *state)}, failure);
		if (process_definition(*state).native == NativeProcessKind::Generation)
			return true;
		if (prepare_rule_upkeep_payments(allocations))
			return true;
	}
}

auto Runtime::finish_rule_upkeep() -> void
{
	const auto now = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	for (auto &[key, plan] : rule_upkeep_plans)
	{
		RuleExecutionState *state = plan.ability_ship || plan.process_owner ? nullptr : find_rule_execution(key);
		std::vector<RuleWorkCredit> *credits = nullptr;
		if (plan.ability_ship)
		{
			const auto ship = entities.find(plan.ability_ship);
			if (ship != entities.end())
				if (auto *runtime = ship->second.try_get_mut<AbilityRuntime>())
					if (const auto grant = runtime->grants.find(plan.ability); grant != runtime->grants.end())
						credits = &grant->second.credits;
		}
		else if (plan.process_owner)
		{
			if (auto *process = process_state(*plan.process_owner, plan.process);
			    process && process->admitted && process->generation == plan.process_generation &&
			    process->run == plan.process_run)
				credits = plan.process_work ? &process->work_credits : &process->idle_credits;
		}
		else if (state)
		{
			state->lease_tick = now + 1;
			state->lease_permille = plan.rejected ? 0 : plan.payment.output;
			state->blocker = plan.rejected ? plan.failure : plan.payment.shortage ? "Upkeep partially funded" : "";
			if (plan.rejected && plan.shortage == "stop")
				state->stop_tick = now + 1;
			credits = &state->credits;
		}
		if (credits && !plan.rejected)
			apply_rule_work_credits(*credits, plan);
	}
}
} // namespace phoenix::world_detail
