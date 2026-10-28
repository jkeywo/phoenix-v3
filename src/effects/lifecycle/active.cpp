module;
#include <flecs.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

module phoenix.effects.active_rules;

namespace phoenix
{
void ActiveRuleStore::initialise(flecs::world &ecs)
{
	query_ = ecs.query_builder<const ActiveRuleEffect>().cached().build();
}
std::size_t ActiveRuleStore::size() const
{
	return index_.size();
}
std::uint64_t ActiveRuleStore::next_identity() const
{
	return next_;
}
void ActiveRuleStore::set_next_identity(std::uint64_t value)
{
	next_ = value;
}

const std::vector<flecs::entity> &ActiveRuleStore::rows() const
{
	if (order_dirty_)
	{
		order_.clear();
		query_.each(
		    [&](flecs::entity e, const ActiveRuleEffect &)
		    {
			    order_.push_back(e);
		    });
		std::sort(order_.begin(), order_.end(),
		          [](auto a, auto b)
		          {
			          return a.template get<ActiveRuleEffect>().id < b.template get<ActiveRuleEffect>().id;
		          });
		order_dirty_ = false;
	}
	return order_;
}

std::vector<ActiveRuleEffect> ActiveRuleStore::snapshot() const
{
	std::vector<ActiveRuleEffect> values;
	for (auto effect_entity : rows())
	{
		const auto &effect = effect_entity.get<ActiveRuleEffect>();
		values.push_back(effect);
	}
	return values;
}

flecs::entity ActiveRuleStore::find(std::uint64_t id) const
{
	const auto at = index_.find(id);
	return at == index_.end() ? flecs::entity{} : at->second;
}

void ActiveRuleStore::erase(flecs::entity entity, RuleClockStore &clocks, const ActiveRuleMutationObserver &observer)
{
	const auto &retired = entity.get<ActiveRuleEffect>();
	observer.before_removal(retired);
	clocks.invalidate_capacity();
	index_.erase(entity.get<ActiveRuleEffect>().id);
	entity.destruct();
	order_dirty_ = true;
}

std::size_t ActiveRuleStore::erase_if(const std::function<bool(const ActiveRuleEffect &)> &predicate,
                                      RuleClockStore &clocks, const ActiveRuleMutationObserver &observer)
{
	std::vector<flecs::entity> removed;
	for (auto entity : rows())
		if (predicate(entity.get<ActiveRuleEffect>()))
			removed.push_back(entity);
	for (auto entity : removed)
		erase(entity, clocks, observer);
	return removed.size();
}

void ActiveRuleStore::commit(flecs::world &ecs, std::span<const ActiveRuleEffect> prepared,
                             const RuleTargetResolver &resolve_target, RuleClockStore &clocks,
                             const ActiveRuleMutationObserver &observer)
{
	clocks.invalidate_capacity();
	std::set<std::uint64_t> retained;
	for (const auto &effect : prepared)
		retained.insert(effect.id);
	erase_if(
	    [&](const auto &effect)
	    {
		    return !retained.contains(effect.id);
	    },
	    clocks, observer);
	for (const auto &effect : prepared)
		if (auto entity = find(effect.id))
		{
			if (!(entity.get<ActiveRuleEffect>() == effect))
				entity.set<ActiveRuleEffect>(effect);
		}
		else
		{
			auto created =
			    ecs.entity().set<ActiveRuleEffect>(effect).set<ActiveRuleTarget>({resolve_target(effect.context)});
			index_.emplace(effect.id, created);
			order_dirty_ = true;
		}
}

bool ActiveRuleStore::exists(const std::vector<ActiveRuleEffect> *prepared, const std::string &source,
                             EntityId target) const
{
	if (prepared)
		return std::any_of(prepared->begin(), prepared->end(),
		                   [&](const auto &effect)
		                   {
			                   return effect.source == source && effect.context.target == target;
		                   });
	for (auto effect_entity : rows())
	{
		const auto &effect = effect_entity.get<ActiveRuleEffect>();
		if (effect.source == source && effect.context.target == target)
			return true;
	}
	return false;
}

bool ActiveRuleStore::target_available(const ActiveRuleEffect &effect, const RuleTargetValidity &valid) const
{
	const auto entity = find(effect.id);
	if (!entity)
		return false;
	const auto body = entity.get<ActiveRuleTarget>().body;
	return body.is_alive() && valid(effect.context, body);
}

void ActiveRuleStore::retire_expired(std::int64_t now, const RuleTargetValidity &valid, RuleClockStore &clocks,
                                     const ActiveRuleMutationObserver &mutation,
                                     const ActiveRuleRetirementObserver &retirement)
{
	std::vector<std::uint64_t> expired;
	for (auto effect_entity : rows())
	{
		const auto &effect = effect_entity.get<ActiveRuleEffect>();
		if (effect.expiry <= now || !target_available(effect, valid))
			expired.push_back(effect.id);
	}
	for (const auto id : expired)
	{
		const auto entity = find(id);
		if (!entity)
			continue;
		const auto &owner = entity.get<ActiveRuleEffect>();
		std::vector<RuleExecutionState> retired_clocks;
		for (const auto &[key, state] : owner.clocks)
			retired_clocks.push_back(state);
		for (auto &state : retired_clocks)
		{
			retirement.withdraw(state);
			clocks.forget(state.key);
		}
		if (entity.is_alive())
			erase(entity, clocks, mutation);
		retirement.transaction_changed();
	}
}

} // namespace phoenix
