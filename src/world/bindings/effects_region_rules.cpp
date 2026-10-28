module;
#include "native_headers.hpp"

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::queue_region_rule_event(EntityId id, const std::string &region, RuleTrigger trigger) -> void
{
	const auto bindings = region_rule_bindings.find(region);
	if (bindings == region_rule_bindings.end())
		return;
	NativeRuleContext event;
	event.subject = event.target = {id};
	event.hit = rule_hit_context(event.subject);
	event.region = region;
	event.tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	for (const auto &binding : bindings->second)
		if (rule_templates.at(binding.rule).trigger == trigger && rule_binding_matches(binding, event))
		{
			auto context = bind_rule_context(binding, event);
			enqueue_rule(binding.rule,
			             rule_key("region:" + region + ":" + std::to_string(id), binding.id, context.subject), context,
			             RuleEventPhase::Region, event.tick);
		}
}

auto Runtime::initialise_rule_membership() -> void
{
	if (region_rule_bindings.empty())
		return;
	for (const auto &[id, ship] : entities)
		if (id < field_identity_base && ship.has<Pose>() && !departed(id) && !combat_dead(ship))
		{
			if (!ship.has<RuleRuntime>())
				ship.set<RuleRuntime>({});
			auto &runtime = ship.get_mut<RuleRuntime>();
			if (runtime.membership_initialised)
				continue;
			runtime.membership_initialised = true;
			const auto point = ship.get<Pose>().position;
			for (const auto &region : regions)
				if (region_rule_bindings.contains(region.id) && scenario_contains(region, {point.x, point.y}))
				{
					runtime.regions.insert(region.id);
					queue_region_rule_event(id, region.id, RuleTrigger::Enter);
				}
		}
	step_rule_events(RuleEventPhase::Region);
}

auto Runtime::step_region_rules() -> void
{
	struct Crossing
	{
		std::int64_t fraction;
		EntityId ship;
		std::string region;
		bool enter;
	};
	std::vector<Crossing> crossings;
	for (const auto &[id, ship] : entities)
		if (auto *runtime = ship.try_get_mut<RuleRuntime>(); runtime && runtime->membership_initialised)
		{
			if (!ship.has<Pose>() || departed(id) || combat_dead(ship))
			{
				runtime->regions.clear();
				continue;
			}
			const auto to = ship.get<Pose>().position;
			const auto start = step_starts.find(id);
			const auto from = start == step_starts.end() ? to : start->second;
			for (const auto &region : regions)
				if (region_rule_bindings.contains(region.id))
				{
					bool inside = runtime->regions.contains(region.id);
					const auto spans = scenario_region_intervals(region, {from.x, from.y}, {to.x, to.y});
					const auto first = std::find_if(spans.begin(), spans.end(),
					                                [](const auto &span)
					                                {
						                                return span.end > span.begin;
					                                });
					// Native clipping omits zero-width boundary contact. A sampled
					// occupant leaving that boundary exits at the start, before later entries.
					if (inside && (first == spans.end() || first->begin > 0))
					{
						crossings.push_back({0, id, region.id, false});
						inside = false;
					}
					for (const auto &span : spans)
					{
						if (span.end <= span.begin)
							continue;
						if (!inside)
							crossings.push_back({span.begin, id, region.id, true});
						inside = true;
						if (span.end < scenario_segment_unit)
						{
							crossings.push_back({span.end, id, region.id, false});
							inside = false;
						}
					}
					const bool sampled = scenario_contains(region, {to.x, to.y});
					if (inside != sampled)
						crossings.push_back({scenario_segment_unit, id, region.id, sampled});
					if (sampled)
						runtime->regions.insert(region.id);
					else
						runtime->regions.erase(region.id);
				}
		}
	std::stable_sort(crossings.begin(), crossings.end(),
	                 [](const auto &a, const auto &b)
	                 {
		                 return std::tie(a.fraction, a.ship, a.region) < std::tie(b.fraction, b.ship, b.region);
	                 });
	for (const auto &crossing : crossings)
		queue_region_rule_event(crossing.ship, crossing.region,
		                        crossing.enter ? RuleTrigger::Enter : RuleTrigger::Exit);
	refresh_rule_eligibility();
	step_rule_events(RuleEventPhase::Region);
}
} // namespace phoenix::world_detail
