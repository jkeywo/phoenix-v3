module;
#include <flecs.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

module phoenix.effects.funding;

namespace phoenix
{
std::int64_t process_cost_product(std::int64_t a, std::int64_t b)
{
	if (a < 0 || b < 0 || (b && a > (std::numeric_limits<std::int64_t>::max() / 1000) / b))
		throw std::runtime_error("Process work price exceeds supported arithmetic bounds");
	return a * b;
}

bool same_rule_work_input(const RuleWorkCredit &credit, const RuleUpkeepInput &input)
{
	return credit.payer == input.payer && credit.store == input.store && credit.resource == input.resource &&
	       credit.property == input.property && credit.equipment == input.property_owner.equipment &&
	       credit.officer == input.property_owner.officer && credit.compartment == input.property_owner.compartment &&
	       credit.unit_work == input.unit_work;
}

void apply_rule_work_credits(std::vector<RuleWorkCredit> &credits, const RuleUpkeepPlan &plan)
{
	for (std::size_t i = 0; i < plan.inputs.size(); ++i)
	{
		const auto &input = plan.inputs[i];
		auto credit = std::find_if(credits.begin(), credits.end(),
		                           [&](const auto &value)
		                           {
			                           return same_rule_work_input(value, input);
		                           });
		if (credit == credits.end())
			credits.push_back({input.payer, input.store, input.resource, plan.payment.credits[i], input.property,
			                   input.property_owner.equipment, input.property_owner.officer,
			                   input.property_owner.compartment, input.unit_work});
		else
			credit->prepaid = plan.payment.credits[i];
	}
}

std::int64_t rule_upkeep_credit(const RuleUpkeepPlan &plan, const RuleUpkeepInput &input)
{
	const auto found = std::find_if(plan.credits.begin(), plan.credits.end(),
	                                [&](const auto &credit)
	                                {
		                                return same_rule_work_input(credit, input);
	                                });
	return found == plan.credits.end() ? 0 : found->prepaid;
}

void reject_rule_upkeep(RuleUpkeepPlan &plan, const std::string &reason)
{
	plan.rejected = true;
	plan.dependency_suspended = false;
	plan.failure = reason;
	plan.payment = {};
	plan.payment.output = 0;
	plan.payment.shortage = true;
}

void resolve_rule_upkeep(const std::string &key, RuleUpkeepPlan &plan, const PropertyCatalog &properties,
                         const RuleFundingPorts &ports,
                         std::set<std::pair<CustomPropertyAddress, std::string>> *paid_stops)
{
	// Pointer identities coalesce aliases, but only canonical native names survive
	// preparation or enter persisted credits.
	std::map<std::tuple<EntityId, StockStore *, std::string>, RuleUpkeepInput> distinct;
	std::map<std::pair<CustomPropertyAddress, std::string>, RuleUpkeepInput> quantities;
	EffectGroupInvocation aliases;
	aliases.rule_context = plan.context;
	for (const auto &cost : plan.costs)
	{
		const auto actor = rule_actor(plan.context, cost.payer);
		if (actor.ordnance || cost.amount < 0 ||
		    cost.amount > (plan.process_owner ? std::numeric_limits<std::int64_t>::max() / 1000 : resource_limit))
			throw std::runtime_error("rules: invalid upkeep payer or rate");
		auto ship = ports.target(actor.entity);
		if (!cost.equipment.empty() &&
		    !ports.equipment_available(actor.entity, ports.equipment(aliases, cost.equipment)))
			throw std::runtime_error("rules: upkeep equipment unavailable");
		if (!cost.property.empty())
		{
			auto property_actor = actor;
			if (!cost.equipment.empty())
			{
				property_actor.equipment = ports.equipment(aliases, cost.equipment);
				property_actor.officer.clear();
				property_actor.compartment = -1;
			}
			const auto address = custom_property_address(property_actor);
			const auto *state = ports.property(address);
			if (!state || !state->bases.contains(cost.property) ||
			    properties.at(cost.property).kind != CustomPropertyKind::Quantity)
				throw std::runtime_error("rules: required quantity property unavailable");
			RuleUpkeepInput input;
			input.payer = actor.entity;
			input.property = cost.property;
			input.property_owner = address;
			input.rate = cost.amount;
			input.unit_work = plan.unit_work;
			auto [it, inserted] = quantities.try_emplace(std::pair{address, cost.property}, input);
			if (!inserted)
			{
				if (it->second.rate >
				    (plan.process_owner ? std::numeric_limits<std::int64_t>::max() / 1000 : resource_limit) -
				        cost.amount)
					throw std::runtime_error("rules: coalesced upkeep rate overflow");
				it->second.rate += cost.amount;
			}
			continue;
		}
		const auto resolved = ports.stock(ship, cost.store);
		auto &stock = resolved.stock;
		if (!stock.accepts(cost.resource))
			throw std::runtime_error("rules: upkeep stock does not accept resource");
		RuleUpkeepInput input;
		input.payer = actor.entity;
		input.resource = cost.resource;
		input.store = resolved.canonical;
		input.rate = cost.amount;
		input.unit_work = plan.unit_work;
		input.network = resolved.kind != NamedStockKind::Physical;
		if (resolved.kind == NamedStockKind::Electrical)
		{
			if (ship.get<ShipResources>().network.definition().stores[resolved.index].resource != cost.resource)
				throw std::runtime_error("rules: upkeep resource does not match electrical reserve");
			input.electrical = resolved.index;
		}
		auto [it, inserted] = distinct.try_emplace(std::tuple{actor.entity, &stock, cost.resource}, input);
		if (!inserted)
		{
			if (it->second.rate >
			    (plan.process_owner ? std::numeric_limits<std::int64_t>::max() / 1000 : resource_limit) - cost.amount)
				throw std::runtime_error("rules: coalesced upkeep rate overflow");
			it->second.rate += cost.amount;
		}
	}
	for (const auto &group : plan.extra_fees)
	{
		if (group.stop && paid_stops && !paid_stops->insert(*group.stop).second)
			continue;
		for (const auto &cost : group.costs)
		{
			RuleUpkeepInput input;
			input.payer = cost.payer;
			input.property = cost.property;
			input.rate = process_cost_product(cost.amount, plan.unit_work / 1000);
			input.unit_work = plan.unit_work;
			RuleUpkeepInput *existing = nullptr;
			if (cost.property_owner)
			{
				input.property_owner = *cost.property_owner;
				const auto inserted = quantities.try_emplace(std::pair{input.property_owner, input.property}, input);
				if (!inserted.second)
					existing = &inserted.first->second;
			}
			else
			{
				const auto resolved = ports.stock(ports.target(cost.payer), cost.store);
				input.store = resolved.canonical;
				input.resource = cost.resource;
				input.network = resolved.kind != NamedStockKind::Physical;
				input.electrical = resolved.kind == NamedStockKind::Electrical ? resolved.index : no_resource;
				const auto inserted =
				    distinct.try_emplace(std::tuple{input.payer, &resolved.stock, input.resource}, input);
				if (!inserted.second)
					existing = &inserted.first->second;
			}
			if (existing)
			{
				if (existing->rate > std::numeric_limits<std::int64_t>::max() / 1000 - input.rate)
					throw std::runtime_error("Process fee bundle arithmetic overflow");
				existing->rate += input.rate;
			}
		}
	}
	for (auto &[identity, input] : distinct)
		plan.inputs.push_back(std::move(input));
	for (auto &[identity, input] : quantities)
		plan.inputs.push_back(std::move(input));
	std::sort(plan.inputs.begin(), plan.inputs.end(),
	          [](const auto &a, const auto &b)
	          {
		          return std::tie(a.payer, a.store, a.resource, a.property_owner, a.property) <
		                 std::tie(b.payer, b.store, b.resource, b.property_owner, b.property);
	          });
	std::size_t new_credits = 0;
	for (std::size_t i = 0; i < plan.inputs.size(); ++i)
	{
		auto &input = plan.inputs[i];
		input.credit = rule_upkeep_credit(plan, input);
		input.request = "rule:" + key + ":cost:" + std::to_string(i);
		if (input.request.size() > 512)
			throw std::runtime_error("rules: upkeep request identity exceeds supported bounds");
		if (std::none_of(plan.credits.begin(), plan.credits.end(),
		                 [&](const auto &value)
		                 {
			                 return same_rule_work_input(value, input);
		                 }))
			++new_credits;
	}
	if (plan.credits.size() + new_credits > 4096)
		throw std::runtime_error("rules: upkeep credit capacity exhausted");
}
RuleUpkeepPlan make_rule_upkeep_plan(const RuleExecutionState &state, std::span<const EffectCostDefinition> costs,
                                     const std::string &shortage)
{
	RuleUpkeepPlan plan;
	plan.context = state.context;
	if (costs.size() > 4096 || state.credits.size() > 4096)
		throw std::runtime_error("rules: upkeep input capacity exhausted");
	plan.costs.assign(costs.begin(), costs.end());
	plan.credits = state.credits;
	plan.shortage = shortage;
	return plan;
}
RuleUpkeepPlan make_ability_upkeep_plan(EntityId ship, const NativeRuleContext &context, const AbilityGrantState &grant,
                                        const AbilityDefinition &definition,
                                        std::span<const EffectCostDefinition> costs)
{
	RuleUpkeepPlan plan;
	plan.context = context;
	plan.costs.assign(costs.begin(), costs.end());
	for (auto &cost : plan.costs)
		if (cost.payer == "protected")
			cost.payer = "subject";
		else if (cost.payer == "attacker")
			cost.payer = "target";
	plan.credits = grant.credits;
	plan.shortage = definition.shortage;
	plan.ability_ship = ship;
	plan.ability = grant.id;
	return plan;
}
RuleUpkeepPlan make_process_upkeep_plan(const CustomPropertyAddress &address, const NativeRuleContext &context,
                                        const ProcessState &state, const ProcessDefinition &definition, bool work,
                                        std::int64_t actual_work, std::int64_t scale, const RuleUpkeepPlan *previous)
{
	RuleUpkeepPlan plan;
	plan.context = context;
	plan.costs = work ? definition.work_costs : definition.upkeep_costs;
	plan.credits = work ? state.work_credits : state.idle_credits;
	plan.shortage = definition.shortage;
	plan.process_owner = address;
	plan.process = state.id;
	plan.process_generation = state.generation;
	plan.process_run = state.run;
	plan.process_work = work;
	plan.producer = definition.native == NativeProcessKind::Generation;
	plan.quoted_work = actual_work;
	plan.quoted_scale = scale;
	plan.unit_work = work ? definition.work_divisor * 1000000 : resource_work_unit * 1000;
	try
	{
		if ((definition.work_divisor != 1 && definition.work_divisor != 1000) || actual_work < 0 || scale < 0 ||
		    scale > 1000000)
			throw std::runtime_error("Invalid process work quotation");
		for (auto &cost : plan.costs)
			cost.amount = process_cost_product(process_cost_product(cost.amount, work ? actual_work : 1), scale);
	}
	catch (const std::exception &error)
	{
		reject_rule_upkeep(plan, error.what());
	}
	if (work)
		plan.depends_on = process_funding_key(address, state, false);
	if (previous)
	{
		if (previous->quoted_work == actual_work && previous->quoted_scale == scale)
			plan.maximum_output = previous->maximum_output;
		if (previous->rejected && !previous->provisional_shortage)
			reject_rule_upkeep(plan, previous->failure);
	}
	return plan;
}

std::int64_t rule_upkeep_request_rate(const RuleUpkeepInput &input, std::int64_t output)
{
	const auto outstanding = std::max(std::int64_t{0}, input.rate * output - input.credit);
	const auto conversion = input.unit_work / resource_work_unit * 1000;
	return outstanding / conversion + (outstanding % conversion != 0);
}
RuleWorkPayment quote_raw_upkeep(RuleUpkeepPlan &plan, std::span<const RuleWorkInput> inputs,
                                 std::optional<std::int64_t> dependency_output)
{
	auto maximum = plan.maximum_output;
	if (!plan.depends_on.empty())
		maximum = std::min(maximum, dependency_output && *dependency_output > 0 ? std::int64_t{1000} : std::int64_t{0});
	auto bound = quote_rule_work(inputs, true, maximum);
	plan.requested_output = bound.output;
	if (bound.shortage && plan.shortage != "proportional")
	{
		reject_rule_upkeep(plan, "Required upkeep stock unavailable");
		plan.provisional_shortage = true;
	}
	return bound;
}
bool quote_funded_upkeep(RuleUpkeepPlan &plan, std::span<const RuleWorkInput> inputs, bool blocked,
                         std::optional<std::int64_t> dependency_output)
{
	auto maximum = plan.maximum_output;
	if (!plan.depends_on.empty())
		maximum = std::min(maximum, dependency_output && *dependency_output > 0 ? std::int64_t{1000} : std::int64_t{0});
	plan.payment = quote_rule_work(inputs, plan.shortage == "proportional", maximum);
	if (blocked && plan.shortage == "proportional")
	{
		plan.payment.output = 0;
		plan.payment.shortage = true;
		for (std::size_t i = 0; i < inputs.size(); ++i)
		{
			plan.payment.debits[i] = 0;
			plan.payment.credits[i] = inputs[i].credit + inputs[i].purchased;
		}
	}
	if ((plan.payment.shortage || blocked) && plan.shortage != "proportional")
		throw std::runtime_error("Required upkeep work unavailable");
	if (plan.payment.output < plan.requested_output && std::any_of(
	                                                       plan.inputs.begin(), plan.inputs.end(),
	                                                       [](const auto &input)
	                                                       {
		                                                       return input.electrical == no_resource;
	                                                       }))
	{
		plan.maximum_output = plan.payment.output;
		return true;
	}
	return false;
}

} // namespace phoenix
