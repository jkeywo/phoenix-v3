module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.effects.clocks;

namespace phoenix
{
void RuleClockStore::begin_rebuild()
{
	capacity_.reset();
	addresses_.clear();
}
void RuleClockStore::index_runtime(EntityId owner, const RuleRuntime &runtime)
{
	for (const auto &[key, state] : runtime.executions)
		addresses_.emplace(key, RuleClockAddress{owner, 0});
}
void RuleClockStore::index_active(const ActiveRuleEffect &effect)
{
	for (const auto &[key, state] : effect.clocks)
		addresses_.emplace(key, RuleClockAddress{0, effect.id});
}
void RuleClockStore::finish_rebuild() const
{
	if (addresses_.size() > rule_instance_limit)
		throw std::runtime_error("rules: execution capacity exhausted");
}
std::size_t RuleClockStore::size() const
{
	return addresses_.size();
}
const std::map<std::string, RuleClockAddress> &RuleClockStore::addresses() const
{
	return addresses_;
}
std::optional<RuleClockAddress> RuleClockStore::address(const std::string &key) const
{
	const auto found = addresses_.find(key);
	return found == addresses_.end() ? std::nullopt : std::optional<RuleClockAddress>{found->second};
}
RuleExecutionState *RuleClockStore::find(const std::string &key, const RuleClockStorage &storage)
{
	const auto found = addresses_.find(key);
	if (found == addresses_.end())
		return nullptr;
	if (found->second.active)
	{
		auto *effect = storage.active(found->second.active);
		if (!effect)
			return nullptr;
		const auto state = effect->clocks.find(key);
		return state == effect->clocks.end() ? nullptr : &state->second;
	}
	auto *runtime = storage.runtime(found->second.ship, false);
	if (!runtime)
		return nullptr;
	const auto state = runtime->executions.find(key);
	return state == runtime->executions.end() ? nullptr : &state->second;
}
const RuleExecutionState *RuleClockStore::read(const std::string &key, const RuleClockReadStorage &storage) const
{
	const auto found = addresses_.find(key);
	if (found == addresses_.end())
		return nullptr;
	if (found->second.active)
	{
		const auto *effect = storage.active(found->second.active);
		if (!effect)
			return nullptr;
		const auto state = effect->clocks.find(key);
		return state == effect->clocks.end() ? nullptr : &state->second;
	}
	const auto *runtime = storage.runtime(found->second.ship);
	if (!runtime)
		return nullptr;
	const auto state = runtime->executions.find(key);
	return state == runtime->executions.end() ? nullptr : &state->second;
}
RuleExecutionState *RuleClockStore::ensure(const NativeRuleSource &source, const RuleBinding &binding,
                                           const NativeRuleContext &context, bool create, const RulesCatalog &rules,
                                           const std::map<std::string, PeriodicEffectDefinition> &periodic,
                                           const RuleClockStorage &storage)
{
	const auto key = rule_key(source.key, binding.id, context.subject);
	if (auto *existing = find(key, storage))
		return existing;
	if (!create)
		return nullptr;
	if (addresses_.size() >= rule_instance_limit)
		throw std::runtime_error("rules: execution capacity exhausted");
	std::map<std::string, RuleExecutionState> *clocks = nullptr;
	std::uint64_t generation = 1;
	std::int64_t application = context.tick;
	RuleClockAddress address;
	if (source.active_effect)
	{
		if (auto *effect = storage.active(source.active_effect))
		{
			clocks = &effect->clocks;
			address.active = effect->id;
			if (periodic.contains(effect->definition))
				application = effect->expiry - periodic.at(effect->definition).duration_ticks;
		}
	}
	else
	{
		address.ship = source.owner.entity ? source.owner.entity : source.context.subject.entity;
		auto *runtime = storage.runtime(address.ship, true);
		if (!runtime)
			return nullptr;
		if (runtime->next_generation >= 9007199254740991ULL)
			throw std::runtime_error("rules: execution generation exhausted");
		generation = runtime->next_generation++;
		clocks = &runtime->executions;
	}
	if (!clocks)
		return nullptr;
	RuleExecutionState state;
	state.key = key;
	state.definition = binding.rule;
	state.binding = binding.id;
	state.source_key = source.key;
	state.owner_kind = source.kind;
	state.context = context;
	state.generation = generation;
	state.started = application;
	state.next = application + rules.at(binding.rule).period_ticks;
	auto &result = clocks->emplace(key, std::move(state)).first->second;
	addresses_.emplace(key, address);
	capacity_.reset();
	return &result;
}
void RuleClockStore::erase(const std::string &key, const RuleClockStorage &storage)
{
	const auto found = addresses_.find(key);
	if (found == addresses_.end())
		return;
	if (found->second.active)
	{
		if (auto *effect = storage.active(found->second.active))
			effect->clocks.erase(key);
	}
	else if (auto *runtime = storage.runtime(found->second.ship, false))
		runtime->executions.erase(key);
	addresses_.erase(found);
	capacity_.reset();
}
void RuleClockStore::forget(const std::string &key)
{
	addresses_.erase(key);
}
void RuleClockStore::invalidate_capacity()
{
	capacity_.reset();
}
std::size_t RuleClockStore::capacity(const RuleClockSources &sources, const std::vector<ActiveRuleEffect> *prepared,
                                     const std::map<std::string, PeriodicEffectDefinition> &periodic) const
{
	if (!prepared && capacity_)
		return *capacity_;
	std::size_t clocks = 0;
	sources.runtimes(
	    [&](const RuleRuntime &runtime)
	    {
		    clocks += runtime.executions.size();
	    });
	const auto include = [&](const ActiveRuleEffect &effect)
	{
		clocks += effect.clocks.size();
		if (const auto found = periodic.find(effect.definition); found != periodic.end())
			for (const auto &binding : found->second.rules)
				if (std::none_of(effect.clocks.begin(), effect.clocks.end(),
				                 [&](const auto &entry)
				                 {
					                 return entry.second.binding == binding.id &&
					                        entry.second.definition == binding.rule;
				                 }))
					++clocks;
	};
	if (prepared)
		for (const auto &effect : *prepared)
			include(effect);
	else
	{
		sources.active(include);
		capacity_ = clocks;
	}
	return clocks;
}

std::vector<PendingRuleExecution> RuleClockStore::advance_due(
    std::span<const NativeRuleSource> sources, const RulesCatalog &rules,
    const std::map<std::string, PeriodicEffectDefinition> &periodic, const RuleClockStorage &storage,
    const std::function<bool(const RuleBinding &, const NativeRuleContext &)> &matches, std::int64_t now)
{
	std::vector<PendingRuleExecution> due;
	for (const auto &source : sources)
		if (source.eligible && source.bindings)
			for (const auto &binding : *source.bindings)
			{
				const auto &rule = rules.at(binding.rule);
				if (rule.phase != RulePhase::Periodic ||
				    (rule.trigger != RuleTrigger::Tick && rule.trigger != RuleTrigger::Occupancy))
					continue;
				auto context = bind_rule_context(binding, source.context);
				const auto key = rule_key(source.key, binding.id, context.subject);
				const bool fresh = !find(key, storage);
				auto *state = ensure(source, binding, context, true, rules, periodic, storage);
				if (!state)
					continue;
				if (fresh)
				{
					state->eligible = true;
					state->active = true;
					state->output = rule.funding ? 0 : 1000;
				}
				if (!state->eligible || state->stopped || state->next > now || state->last == now)
					continue;
				state->next = now + rule.period_ticks;
				state->last = now;
				if (!matches(binding, source.context) || (rule.funding && !state->output))
					continue;
				context.tick = now;
				context.elapsed = std::max<std::int64_t>(0, now - state->started);
				context.output = state->output;
				state->context = context;
				due.push_back({0, now, RuleEventPhase::Periodic, state->definition, key, 0, std::move(context)});
			}
	return due;
}

} // namespace phoenix
