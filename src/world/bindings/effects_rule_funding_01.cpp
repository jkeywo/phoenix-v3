module;
#include <flecs.h>
#include <functional>
#include <chrono>
#include <charconv>
#include <iterator>
#include <type_traits>
#include <string_view>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <array>
#include <variant>
#include <set>
#include <stdexcept>
#include <cstdint>
#include <map>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <tuple>
#include <vector>

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::clear_rule_upkeep() -> void
{
	rule_upkeep_plans.clear();
	rule_upkeep_effects.reset();
	process_producer_quote.reset();
	process_producer_networks.clear();
	process_exit_failures.clear();
	process_funding_rejection_pending = false;
}

auto Runtime::stage_rule_upkeep(const RuleExecutionState &state, std::span<const EffectCostDefinition> costs,
                                const std::string &shortage) -> void
{
	if (rule_upkeep_plans.size() >= rule_instance_limit && !rule_upkeep_plans.contains(state.key))
		throw std::runtime_error("rules: upkeep plan capacity exhausted");
	rule_upkeep_plans.insert_or_assign(state.key, make_rule_upkeep_plan(state, costs, shortage));
}

auto Runtime::ability_upkeep_key(EntityId ship, const std::string &grant) const -> std::string
{
	return "ability-upkeep:" + std::to_string(ship) + ":" + grant;
}

auto Runtime::stage_ability_upkeep(EntityId ship, const AbilityGrantState &grant, const AbilityDefinition &definition,
                                   std::span<const EffectCostDefinition> costs) -> void
{
	if (rule_upkeep_plans.size() >= rule_instance_limit)
		throw std::runtime_error("rules: upkeep plan capacity exhausted");
	const auto invocation = ability_effect_invocation(ship, grant, {});
	auto plan = make_ability_upkeep_plan(ship, *invocation.rule_context, grant, definition, costs);
	rule_upkeep_plans.insert_or_assign(ability_upkeep_key(ship, grant.id), std::move(plan));
}

auto Runtime::ability_upkeep_output(EntityId ship, const std::string &grant) const -> std::int64_t
{
	const auto at = rule_upkeep_plans.find(ability_upkeep_key(ship, grant));
	return at == rule_upkeep_plans.end() || at->second.rejected ? 0 : at->second.payment.output;
}

auto Runtime::stage_process_costs(const CustomPropertyAddress &address, const ProcessState &state,
                                  const ProcessDefinition &definition, bool work, std::int64_t actual_work,
                                  std::int64_t scale) -> void
{
	const auto key = process_funding_key(address, state, work);
	if (rule_upkeep_plans.size() >= rule_instance_limit && !rule_upkeep_plans.contains(key))
		throw std::runtime_error("Process funding capacity exhausted");
	const auto context = process_context(address, state);
	const auto previous = rule_upkeep_plans.find(key);
	auto plan = make_process_upkeep_plan(address, context, state, definition, work, actual_work, scale,
	                                     previous == rule_upkeep_plans.end() ? nullptr : &previous->second);
	rule_upkeep_plans.insert_or_assign(key, std::move(plan));
	if (work && rule_upkeep_plans.at(key).rejected)
		reject_process_funding(address, state.id, rule_upkeep_plans.at(key).failure);
}

auto Runtime::append_process_fee_group(RuleUpkeepPlan &plan, const EffectGroupInvocation &initial) -> void
{
	PreparedEffectGroup scope;
	scope.invocations.push_back(initial);
	expand_process_effect_frames(
	    scope,
	    [&](std::size_t i, const EffectGroupInvocation &frame)
	    {
		    ProcessFeeGroup fees;
		    if (i)
			    fees.stop = std::pair{custom_property_address(frame.rule_context->owner), frame.source};
		    fees.costs = resolve_effect_costs(scope, frame);
		    if (!fees.costs.empty())
			    plan.extra_fees.push_back(std::move(fees));
		    return plan.producer;
	    },
	    "Producer lifecycle fee expansion capacity exhausted");
}

auto Runtime::stage_process_upkeep(const CustomPropertyAddress &address, const ProcessState &state,
                                   const ProcessDefinition &definition) -> void
{
	const auto scale = process_scale(definition.upkeep_scale, process_expression_context(address, state));
	stage_process_costs(address, state, definition, false, 0, scale.value_or(-1));
	auto &plan = rule_upkeep_plans.at(process_funding_key(address, state, false));
	if (!plan.producer || plan.rejected)
		return;
	try
	{
		if (const auto resume = process_funding_effect(address, state, 1000, 1000))
		{
			append_process_fee_group(plan, process_effect_invocation(address, state, *resume));
			plan.resume_fees = true;
		}
	}
	catch (const std::exception &error)
	{
		reject_rule_upkeep(plan, error.what());
	}
}

auto Runtime::stage_process_work(const CustomPropertyAddress &address, const ProcessState &state,
                                 const ProcessDefinition &definition, std::int64_t actual_work, std::int64_t activity)
    -> void
{
	const auto scale =
	    process_scale(definition.work_scale, process_expression_context(address, state, actual_work, activity));
	stage_process_costs(address, state, definition, true, actual_work, scale.value_or(-1));
	auto &plan = rule_upkeep_plans.at(process_funding_key(address, state, true));
	if (plan.rejected || !actual_work)
		return;
	try
	{
		const auto evaluation = process_work_effects(address, state, actual_work, activity);
		if (evaluation.status != RuleEvaluationStatus::Ready)
			throw std::runtime_error("Invalid process work effect or arithmetic");
		append_process_fee_group(plan, process_effect_invocation(address, state, evaluation.quote.effects));
	}
	catch (const std::exception &error)
	{
		reject_process_funding(address, state.id, error.what());
	}
}

auto Runtime::process_funding_output(const CustomPropertyAddress &address, const std::string &id, bool work) const
    -> std::int64_t
{
	const auto *state = process_state(address, id);
	if (!state)
		return 0;
	const auto found = rule_upkeep_plans.find(process_funding_key(address, *state, work));
	return found == rule_upkeep_plans.end() || found->second.rejected ? 0 : found->second.payment.output;
}

auto Runtime::reject_process_funding(const CustomPropertyAddress &address, const std::string &id,
                                     const std::string &reason, bool work) -> bool
{
	const auto *state = process_state(address, id);
	if (!state)
		return false;
	const auto found = rule_upkeep_plans.find(process_funding_key(address, *state, work));
	if (found == rule_upkeep_plans.end())
		return false;
	const auto changed = !found->second.rejected;
	if (changed)
		reject_rule_upkeep(found->second, reason);
	if (work)
	{
		const auto idle = rule_upkeep_plans.find(process_funding_key(address, *state, false));
		if (idle != rule_upkeep_plans.end())
			reject_rule_upkeep(idle->second, reason);
	}
	if (changed)
		process_funding_rejection_pending = true;
	return changed;
}

auto Runtime::release_suspended_process_plans() -> void
{
	for (auto &[key, plan] : rule_upkeep_plans)
		if (!plan.rejected || plan.provisional_shortage || plan.dependency_suspended)
			plan.maximum_output = 1000;
	for (auto &[key, plan] : rule_upkeep_plans)
		if (plan.dependency_suspended)
		{
			plan.dependency_suspended = plan.rejected = false;
			plan.failure.clear();
			plan.maximum_output = 1000;
			if (const auto *state = plan.process_owner ? process_state(*plan.process_owner, plan.process) : nullptr)
			{
				const auto work = rule_upkeep_plans.find(process_funding_key(*plan.process_owner, *state, true));
				if (work != rule_upkeep_plans.end() && work->second.provisional_shortage)
				{
					work->second.provisional_shortage = work->second.rejected = false;
					work->second.failure.clear();
					work->second.maximum_output = 1000;
				}
			}
		}
}

auto Runtime::resolve_rule_upkeep(const std::string &key, RuleUpkeepPlan &plan,
                                  std::set<std::pair<CustomPropertyAddress, std::string>> *paid_stops) -> void
{
	RuleFundingPorts ports;
	ports.target = [this](EntityId id)
	{
		return effect_group_target(id);
	};
	ports.equipment = [this](const EffectGroupInvocation &frame, const std::string &binding)
	{
		return effect_group_equipment(frame, binding);
	};
	ports.equipment_available = [this](EntityId id, const std::string &equipment)
	{
		return effect_group_equipment_available(id, equipment);
	};
	ports.property = [this](const CustomPropertyAddress &address)
	{
		return custom_property_state(address);
	};
	ports.stock = [this](flecs::entity body, const std::string &name)
	{
		return named_stock(body, name);
	};
	phoenix::resolve_rule_upkeep(key, plan, *property_catalogue, ports, paid_stops);
}

auto Runtime::process_producer_seed(EntityId id) const -> const ResourceNetwork *
{
	const auto found = process_producer_networks.find(id);
	return found == process_producer_networks.end() ? nullptr : &found->second;
}

auto Runtime::prepare_producer_exit_fees(PreparedEffectGroup &quote, const RuleUpkeepPlan &plan) -> void
{
	const auto &address = *plan.process_owner;
	const auto *state = process_state(address, plan.process);
	if (!state)
		return;
	auto candidate = *state;
	const auto edge = stage_process_funding_state(address, candidate, process_funding_output(address, state->id, false),
	                                              process_funding_output(address, state->id, true));
	if (edge != "pause" && edge != "stop")
		return;
	const auto group = process_funding_effect(address, *state, process_funding_output(address, state->id, false),
	                                          process_funding_output(address, state->id, true));
	if (!group)
		return;
	const auto key = std::pair{address, process_run_source(address, *state)};
	if (!process_exit_failures.contains(key))
	{
		const auto stocks = quote.stocks;
		const auto properties = quote.custom_properties, originals = quote.original_custom_properties;
		const auto inventories = quote.changed_inventories;
		const auto stops = quote.prepaid_process_stops;
		const auto targets = quote.targets.size(), equipment = quote.equipment.size();
		try
		{
			PreparedEffectGroup scope;
			scope.invocations.push_back(process_effect_invocation(address, *state, *group));
			expand_process_effect_frames(
			    scope,
			    [&](std::size_t i, const EffectGroupInvocation &frame)
			    {
				    const auto stop_key = std::pair{custom_property_address(frame.rule_context->owner), frame.source};
				    if ((i || edge == "stop") && quote.prepaid_process_stops.contains(stop_key))
					    return false;
				    const auto costs = resolve_effect_costs(quote, frame);
				    stage_effect_costs(quote, frame, costs);
				    if (i || edge == "stop")
					    quote.prepaid_process_stops.insert(stop_key);
				    return true;
			    },
			    "Producer exit fee expansion capacity exhausted");
			quote.prepaid_process_exits.insert(key);
		}
		catch (const std::exception &error)
		{
			quote.stocks = stocks;
			quote.custom_properties = properties;
			quote.original_custom_properties = originals;
			quote.changed_inventories = inventories;
			quote.prepaid_process_stops = stops;
			quote.targets.resize(targets);
			quote.equipment.resize(equipment);
			process_exit_failures.emplace(key, error.what());
		}
	}
	if (const auto failure = process_exit_failures.find(key); failure != process_exit_failures.end())
		quote.effect_failures.push_back("process exit " + key.second + ": " + failure->second);
}

auto Runtime::prepare_process_producer_funding() -> bool
{
	process_producer_quote = std::make_unique<PreparedEffectGroup>(prepare_effect_groups({}));
	process_producer_networks.clear();
	for (const auto &[id, entity] : entities)
		if (const auto *power = entity.try_get<ShipResources>())
			process_producer_quote->payments.try_emplace(id, power->network);
	auto &quote = *process_producer_quote;
	quote.pre_generation = true;
	bool retry = false;
	std::string producer;
	StockTransaction before_inputs;
	std::map<CustomPropertyAddress, CustomPropertyState> before_properties, before_original_properties;
	std::set<EntityId> before_inventories;
	std::set<std::pair<CustomPropertyAddress, std::string>> paid_stops, before_stops;
	for (auto &[key, plan] : rule_upkeep_plans)
		if (plan.producer && plan.provisional_shortage)
		{
			plan.rejected = plan.provisional_shortage = false;
			plan.failure.clear();
		}
	for (auto &[key, plan] : rule_upkeep_plans)
	{
		if (!plan.producer || plan.rejected)
			continue;
		const auto *owner_state = plan.process_owner ? process_state(*plan.process_owner, plan.process) : nullptr;
		if (plan.process_owner && !owner_state)
		{
			reject_rule_upkeep(plan, "Producer owner unavailable");
			retry = true;
			continue;
		}
		const auto owner_key = owner_state ? process_instance_key(*plan.process_owner, *owner_state) : key;
		if (producer != owner_key)
		{
			producer = owner_key;
			before_inputs = quote.stocks;
			before_properties = quote.custom_properties;
			before_original_properties = quote.original_custom_properties;
			before_inventories = quote.changed_inventories;
			before_stops = paid_stops;
		}
		try
		{
			plan.inputs.clear();
			resolve_rule_upkeep(key, plan, &paid_stops);
			std::vector<RuleWorkInput> inputs;
			std::vector<StockStore *> stocks;
			for (const auto &input : plan.inputs)
			{
				RuleWorkInput work{input.rate, input.credit};
				work.unit_work = input.unit_work;
				if (!input.property.empty())
				{
					auto &state = prepare_custom_property_owner(quote, input.property_owner);
					work.available = std::get<std::int64_t>(state.bases.at(input.property)) -
					                 property_catalogue->at(input.property).minimum;
					stocks.push_back(nullptr);
				}
				else
				{
					auto resolved = named_stock(entities.at(input.payer), input.store,
					                            input.network ? &quote.payments.at(input.payer).network : nullptr);
					work.available = quote.stocks.view(resolved.stock).available(input.resource);
					if (input.electrical != no_resource)
					{
						const auto &network = quote.payments.at(input.payer).network;
						const auto reserved = resolved.stock.quantity(input.resource) -
						                      quote.stocks.view(resolved.stock).quantity(input.resource);
						work.available = std::min(
						    work.available,
						    std::max<std::int64_t>(0, network.producer_input_budget(input.electrical) - reserved));
						work.blocked = !network.producer_input_operational(input.electrical);
					}
					stocks.push_back(&resolved.stock);
					if (!input.network)
						quote.changed_inventories.insert(input.payer);
				}
				// Producer inputs are quoted solely from the pre-generation ledger.
				// No current generator output, even from a peer, enters this view.
				inputs.push_back(work);
			}
			auto maximum = plan.maximum_output;
			if (!plan.depends_on.empty())
			{
				const auto dependency = rule_upkeep_plans.find(plan.depends_on);
				maximum = std::min(maximum, dependency == rule_upkeep_plans.end() || dependency->second.rejected ? 0
				                            : dependency->second.payment.output > 0 ? std::int64_t{1000}
				                                                                    : std::int64_t{0});
			}
			plan.payment = quote_rule_work(inputs, plan.shortage == "proportional", maximum);
			plan.requested_output = plan.payment.output;
			if (plan.payment.shortage && plan.shortage != "proportional")
			{
				reject_rule_upkeep(plan, "Required pre-generation process input unavailable");
				plan.provisional_shortage = true;
				if (plan.process_work && plan.process_owner)
				{
					const auto *state = process_state(*plan.process_owner, plan.process);
					const auto idle = rule_upkeep_plans.find(process_funding_key(*plan.process_owner, *state, false));
					if (idle != rule_upkeep_plans.end())
					{
						reject_rule_upkeep(idle->second, plan.failure);
						idle->second.provisional_shortage = true;
					}
				}
				quote.stocks = before_inputs;
				quote.custom_properties = before_properties;
				quote.original_custom_properties = before_original_properties;
				quote.changed_inventories = before_inventories;
				paid_stops = before_stops;
				continue;
			}
			for (std::size_t i = 0; i < plan.inputs.size(); ++i)
				if (!plan.inputs[i].property.empty())
				{
					auto &value =
					    quote.custom_properties.at(plan.inputs[i].property_owner).bases.at(plan.inputs[i].property);
					value = std::get<std::int64_t>(value) - plan.payment.debits[i];
				}
				else if (plan.payment.debits[i] &&
				         !quote.stocks.consume(*stocks[i], plan.inputs[i].resource, plan.payment.debits[i]))
					throw std::logic_error("Prepared producer input changed");
		}
		catch (const std::exception &error)
		{
			reject_rule_upkeep(plan, error.what());
			quote.stocks = before_inputs;
			quote.custom_properties = before_properties;
			quote.original_custom_properties = before_original_properties;
			quote.changed_inventories = before_inventories;
			paid_stops = before_stops;
			if (plan.process_owner && plan.process_work)
				reject_process_funding(*plan.process_owner, plan.process, error.what(), false);
			retry = true;
		}
	}
	quote.prepaid_process_stops = paid_stops;
	for (const auto &[key, plan] : rule_upkeep_plans)
		if (plan.producer && plan.process_owner && !plan.process_work)
			prepare_producer_exit_fees(quote, plan);
	for (auto &[id, payment] : quote.payments)
	{
		auto network = payment.network;
		for (ResourceIndex i = 0; i < network.stores().size(); ++i)
		{
			const auto &resource = network.definition().stores.at(i).resource;
			const auto debit = payment.network.stock(i).quantity(resource) -
			                   quote.stocks.view(payment.network.stock(i)).quantity(resource);
			if (debit && !network.prepare_producer_payment(i, debit))
				throw std::logic_error("Prepared native producer payment changed");
		}
		for (ResourceIndex i = 0; i < network.coolant_sources().size(); ++i)
			network.coolant_stock(i) = quote.stocks.view(payment.network.coolant_stock(i));
		process_producer_networks.emplace(id, std::move(network));
	}
	return retry;
}

auto Runtime::request_rule_upkeep() -> void
{
	if (process_funding_rejection_pending)
	{
		release_suspended_process_plans();
		process_funding_rejection_pending = false;
	}
	StockTransaction raw = process_producer_quote ? process_producer_quote->stocks : StockTransaction{};
	std::map<std::pair<CustomPropertyAddress, std::string>, std::int64_t> raw_quantities;
	// Rebuild only this mechanism's named requests. Native consumers retain their
	// requests, and every retry starts from authoritative uncommitted quantities.
	for (auto &[key, plan] : rule_upkeep_plans)
	{
		if (plan.producer)
			continue;
		for (const auto &input : plan.inputs)
			if (input.electrical != no_resource && entities.contains(input.payer))
				entities.at(input.payer).get_mut<ShipResources>().named_requests.erase(input.request);
		if (plan.provisional_shortage)
		{
			plan.rejected = false;
			plan.provisional_shortage = false;
			plan.failure.clear();
		}
	}
	for (auto &[key, plan] : rule_upkeep_plans)
	{
		if (plan.rejected || plan.producer)
			continue;
		try
		{
			plan.inputs.clear();
			resolve_rule_upkeep(key, plan);
			std::vector<RuleWorkInput> inputs;
			for (const auto &input : plan.inputs)
				if (input.electrical == no_resource)
				{
					if (!input.property.empty())
					{
						const auto address = std::pair{input.property_owner, input.property};
						const auto &state =
						    process_producer_quote &&
						            process_producer_quote->custom_properties.contains(input.property_owner)
						        ? process_producer_quote->custom_properties.at(input.property_owner)
						        : *custom_property_state(input.property_owner);
						const auto quantity = std::get<std::int64_t>(state.bases.at(input.property)) -
						                      property_catalogue->at(input.property).minimum;
						inputs.push_back(
						    {input.rate, input.credit, 0, raw_quantities.try_emplace(address, quantity).first->second});
					}
					else
					{
						auto &stock = named_stock(entities.at(input.payer), input.store,
						                          input.network && process_producer_networks.contains(input.payer)
						                              ? &process_producer_networks.at(input.payer)
						                              : nullptr)
						                  .stock;
						inputs.push_back({input.rate, input.credit, 0, raw.view(stock).available(input.resource)});
					}
					inputs.back().unit_work = input.unit_work;
				}
			std::optional<std::int64_t> dependency_output;
			if (!plan.depends_on.empty())
			{
				const auto dependency = rule_upkeep_plans.find(plan.depends_on);
				if (dependency != rule_upkeep_plans.end() && !dependency->second.rejected)
					dependency_output = dependency->second.requested_output;
			}
			const auto bound = quote_raw_upkeep(plan, inputs, dependency_output);
			if (plan.provisional_shortage)
				continue;

			std::size_t raw_index = 0;
			for (const auto &input : plan.inputs)
				if (input.electrical == no_resource)
				{
					const auto debit = bound.debits[raw_index++];
					if (!input.property.empty())
						raw_quantities.at({input.property_owner, input.property}) -= debit;
					else
					{
						auto &stock = named_stock(entities.at(input.payer), input.store,
						                          input.network && process_producer_networks.contains(input.payer)
						                              ? &process_producer_networks.at(input.payer)
						                              : nullptr)
						                  .stock;
						if (!raw.consume(stock, input.resource, debit))
							throw std::logic_error("rules: raw upkeep request quote changed");
					}
				}
			for (const auto &input : plan.inputs)
				if (input.electrical != no_resource)
				{
					const auto rate = rule_upkeep_request_rate(input, bound.output);
					request_named_resource(entities.at(input.payer).get_mut<ShipResources>(), input.request,
					                       input.electrical, rate);
				}
		}
		catch (const std::exception &error)
		{
			reject_rule_upkeep(plan, error.what());
			for (const auto &input : plan.inputs)
				if (input.electrical != no_resource && entities.contains(input.payer))
					entities.at(input.payer).get_mut<ShipResources>().named_requests.erase(input.request);
		}
	}
}

auto Runtime::prepare_rule_upkeep_payments(std::map<EntityId, std::optional<ShipResourceAllocation>> &allocations,
                                           bool native_requests_removed) -> bool
{
	rule_upkeep_effects = std::make_unique<PreparedEffectGroup>(prepare_effect_groups({}));
	if (process_producer_quote)
	{
		rule_upkeep_effects->stocks = process_producer_quote->stocks;
		rule_upkeep_effects->prepaid_process_stops = process_producer_quote->prepaid_process_stops;
		rule_upkeep_effects->prepaid_process_exits = process_producer_quote->prepaid_process_exits;
		rule_upkeep_effects->effect_failures = process_producer_quote->effect_failures;
		rule_upkeep_effects->changed_inventories = process_producer_quote->changed_inventories;
		rule_upkeep_effects->custom_properties = process_producer_quote->custom_properties;
		rule_upkeep_effects->original_custom_properties = process_producer_quote->original_custom_properties;
	}
	for (auto &[id, allocation] : allocations)
		if (allocation)
		{
			auto &network = allocation->prepared_network();
			rule_upkeep_effects->payments.try_emplace(id, network, &network);
		}
	bool removed = false;
	bool retired = native_requests_removed;
	for (auto &[key, plan] : rule_upkeep_plans)
	{
		if (plan.rejected || plan.producer)
			continue;
		std::vector<RuleWorkInput> inputs;
		std::vector<StockStore *> stocks;
		bool blocked = false;
		try
		{
			for (const auto &input : plan.inputs)
			{
				RuleWorkInput work{input.rate, input.credit};
				work.unit_work = input.unit_work;
				const auto allocation = allocations.find(input.payer);
				if (input.network && (allocation == allocations.end() || !allocation->second))
					throw std::runtime_error("rules: upkeep allocation unavailable");
				if (input.electrical != no_resource)
				{
					const auto &grants = allocation->second->named_grants();
					const auto grant = grants.find(input.request);
					if (grant == grants.end())
						throw std::runtime_error("rules: upkeep grant unavailable");
					work.electrical = true;
					const auto conversion = input.unit_work / resource_work_unit;
					if (grant->second.work_funded > std::numeric_limits<std::int64_t>::max() / conversion)
						throw std::runtime_error("rules: funded work conversion overflow");
					work.purchased = grant->second.work_funded * conversion;
					work.blocked = grant->second.blocked;
					blocked = blocked || work.blocked;
					stocks.push_back(nullptr);
				}
				else if (!input.property.empty())
				{
					const auto *state = custom_property_state(input.property_owner);
					if (!state || !state->bases.contains(input.property))
						throw std::runtime_error("rules: upkeep quantity owner unavailable");
					auto &prepared = prepare_custom_property_owner(*rule_upkeep_effects, input.property_owner);
					work.available = std::get<std::int64_t>(prepared.bases.at(input.property)) -
					                 property_catalogue->at(input.property).minimum;
					stocks.push_back(nullptr);
				}
				else
				{
					auto resolved =
					    named_stock(entities.at(input.payer), input.store,
					                input.network ? &rule_upkeep_effects->payments.at(input.payer).network : nullptr);
					work.available = rule_upkeep_effects->stocks.view(resolved.stock).available(input.resource);
					stocks.push_back(&resolved.stock);
				}
				inputs.push_back(work);
			}
			std::optional<std::int64_t> dependency_output;
			if (!plan.depends_on.empty())
			{
				const auto dependency = rule_upkeep_plans.find(plan.depends_on);
				if (dependency != rule_upkeep_plans.end() && !dependency->second.rejected)
					dependency_output = dependency->second.payment.output;
			}
			removed = quote_funded_upkeep(plan, inputs, blocked, dependency_output) || removed;

			for (std::size_t i = 0; i < inputs.size(); ++i)
				if (!plan.inputs[i].property.empty() && plan.payment.debits[i])
				{
					const auto &input = plan.inputs[i];
					auto &value =
					    rule_upkeep_effects->custom_properties.at(input.property_owner).bases.at(input.property);
					value = std::get<std::int64_t>(value) - plan.payment.debits[i];
				}
				else if (stocks[i] && plan.payment.debits[i])
				{
					if (!rule_upkeep_effects->stocks.consume(*stocks[i], plan.inputs[i].resource,
					                                         plan.payment.debits[i]))
						throw std::logic_error("rules: prepared upkeep stock changed");
					if (!plan.inputs[i].network)
						rule_upkeep_effects->changed_inventories.insert(plan.inputs[i].payer);
				}
		}
		catch (const std::exception &error)
		{
			reject_rule_upkeep(plan, error.what());
			removed = true;
			retired = true;
			if (plan.process_owner && plan.process_work)
				reject_process_funding(*plan.process_owner, plan.process, error.what(), false);
			for (const auto &input : plan.inputs)
				if (input.electrical != no_resource && entities.contains(input.payer))
					removed = entities.at(input.payer).get_mut<ShipResources>().named_requests.erase(input.request) ||
					          removed;
		}
	}
	if (!removed)
		for (auto &[key, plan] : rule_upkeep_plans)
			if (plan.process_owner && plan.process_work && !plan.producer && plan.provisional_shortage)
			{
				// Withhold upkeep while work lacks a provisional input. A later
				// rejected work effect can still free it, so this is a dependency
				// suspension rather than a permanent admission rejection.
				const auto *state = process_state(*plan.process_owner, plan.process);
				const auto idle = state
				                      ? rule_upkeep_plans.find(process_funding_key(*plan.process_owner, *state, false))
				                      : rule_upkeep_plans.end();
				if (idle != rule_upkeep_plans.end() && !idle->second.rejected)
				{
					reject_rule_upkeep(idle->second, "Required process work input unavailable");
					idle->second.dependency_suspended = true;
					removed = true;
				}
			}
	if (retired)
	{
		release_suspended_process_plans();
		process_funding_rejection_pending = false;
		// Removing an indivisible/native consumer changes the feasible allocation.
		// Survivors may now use its released flow. Only these finite request-set
		// reductions reset caps; between reductions each integer cap decreases
		// at most 1000 times, which bounds requotation without a second scheduler.
		for (auto &[key, plan] : rule_upkeep_plans)
			if (!plan.rejected || plan.provisional_shortage)
				plan.maximum_output = 1000;
	}
	return removed;
}

auto Runtime::commit_rule_upkeep_stocks() -> bool
{
	if (!rule_upkeep_effects)
		return false;
	return commit_effect_group(*rule_upkeep_effects).empty();
}

auto Runtime::append_rule_upkeep_effects(std::span<const EffectGroupInvocation> invocations,
                                         std::size_t &failed_invocation) -> std::string
{
	if (!rule_upkeep_effects || rule_upkeep_effects->consumed)
		return "Upkeep quote unavailable";
	*rule_upkeep_effects = prepare_effect_groups(invocations, std::move(*rule_upkeep_effects));
	failed_invocation = rule_upkeep_effects->failed_invocation;
	return rule_upkeep_effects->failure;
}

auto Runtime::stage_rule_upkeep_process_receipt(const CustomPropertyAddress &address, const std::string &id,
                                                std::int64_t actual_work, std::int64_t idle, std::int64_t activity)
    -> void
{
	if (!rule_upkeep_effects)
		throw std::runtime_error("Process work quote unavailable");
	const auto *state = process_state(address, id);
	if (!state)
		throw std::runtime_error("Process work source unavailable");
	auto &owner = prepare_process_owner(*rule_upkeep_effects, address);
	const auto found = owner.processes.find(id);
	if (found == owner.processes.end())
		return;
	auto &prepared = found->second;
	stage_process_funding_state(address, prepared, idle, process_funding_output(address, id, true));
	if (prepared.phase == "running")
		prepared.work_output = activity;
	if (prepared.admitted)
		prepared.work_remainder = process_next_work_remainder(address, *state, actual_work);
}
} // namespace phoenix::world_detail
