// Included inside World::Impl. These plans live for one resource phase only.
// Authoritative prepaid work and leases remain on their native rule owners.
struct RuleUpkeepInput
{
	EntityId payer = 0;
	std::string store, resource, request;
	std::int64_t rate = 0, credit = 0;
	ResourceIndex electrical = no_resource;
	bool network = false;
	std::string property;
	CustomPropertyAddress property_owner;
	std::int64_t unit_work = resource_work_unit;
};
struct ProcessFeeGroup
{
	std::optional<std::pair<CustomPropertyAddress, std::string>> stop;
	std::vector<PreparedEffectCost> costs;
};
struct RuleUpkeepPlan
{
	NativeRuleContext context;
	std::vector<EffectCostDefinition> costs;
	std::vector<RuleWorkCredit> credits;
	std::vector<RuleUpkeepInput> inputs;
	RuleWorkPayment payment;
	std::string shortage, failure;
	bool rejected = false;
	// Raw reservations are provisional until electrical allocation is feasible.
	bool provisional_shortage = false;
	bool dependency_suspended = false;
	std::int64_t maximum_output = 1000, requested_output = 1000;
	EntityId ability_ship = 0;
	std::string ability;
	std::optional<CustomPropertyAddress> process_owner;
	std::string process;
	bool process_work = false;
	std::int64_t unit_work = resource_work_unit;
	std::string depends_on;
	std::int64_t quoted_work = -1, quoted_scale = 1000;
	bool producer = false;
	std::vector<ProcessFeeGroup> extra_fees;
	bool resume_fees = false;
	std::uint64_t process_generation = 0, process_run = 0;
};
std::map<std::string, RuleUpkeepPlan> rule_upkeep_plans;
std::unique_ptr<PreparedEffectGroup> rule_upkeep_effects;
std::unique_ptr<PreparedEffectGroup> process_producer_quote;
std::map<EntityId, ResourceNetwork> process_producer_networks;
std::map<std::pair<CustomPropertyAddress, std::string>, std::string> process_exit_failures;
bool process_funding_rejection_pending = false;

void clear_rule_upkeep()
{
	rule_upkeep_plans.clear();
	rule_upkeep_effects.reset();
	process_producer_quote.reset();
	process_producer_networks.clear();
	process_exit_failures.clear();
	process_funding_rejection_pending = false;
}
void stage_rule_upkeep(const RuleExecutionState &state, std::span<const EffectCostDefinition> costs,
                       const std::string &shortage)
{
	if (rule_upkeep_plans.size() >= rule_instance_limit && !rule_upkeep_plans.contains(state.key))
		throw std::runtime_error("rules: upkeep plan capacity exhausted");
	RuleUpkeepPlan plan;
	plan.context = state.context;
	if (costs.size() > 4096 || state.credits.size() > 4096)
		throw std::runtime_error("rules: upkeep input capacity exhausted");
	plan.costs.assign(costs.begin(), costs.end());
	plan.credits = state.credits;
	plan.shortage = shortage;
	rule_upkeep_plans.insert_or_assign(state.key, std::move(plan));
}
std::string ability_upkeep_key(EntityId ship, const std::string &grant) const
{
	return "ability-upkeep:" + std::to_string(ship) + ":" + grant;
}
void stage_ability_upkeep(EntityId ship, const AbilityGrantState &grant, const AbilityDefinition &definition,
                          std::span<const EffectCostDefinition> costs)
{
	if (rule_upkeep_plans.size() >= rule_instance_limit)
		throw std::runtime_error("rules: upkeep plan capacity exhausted");
	const auto invocation = ability_effect_invocation(ship, grant, {});
	RuleUpkeepPlan plan;
	plan.context = *invocation.rule_context;
	plan.costs.assign(costs.begin(), costs.end());
	// Preserve the invocation adapter's legacy payer aliases for ongoing costs too.
	for (auto &cost : plan.costs)
		if (cost.payer == "protected")
			cost.payer = "subject";
		else if (cost.payer == "attacker")
			cost.payer = "target";
	plan.credits = grant.credits;
	plan.shortage = definition.shortage;
	plan.ability_ship = ship;
	plan.ability = grant.id;
	rule_upkeep_plans.insert_or_assign(ability_upkeep_key(ship, grant.id), std::move(plan));
}
std::int64_t ability_upkeep_output(EntityId ship, const std::string &grant) const
{
	const auto at = rule_upkeep_plans.find(ability_upkeep_key(ship, grant));
	return at == rule_upkeep_plans.end() || at->second.rejected ? 0 : at->second.payment.output;
}
static std::int64_t process_cost_product(std::int64_t a, std::int64_t b)
{
	if (a < 0 || b < 0 || (b && a > (std::numeric_limits<std::int64_t>::max() / 1000) / b))
		throw std::runtime_error("Process work price exceeds supported arithmetic bounds");
	return a * b;
}
std::string process_funding_key(const CustomPropertyAddress &address, const ProcessState &state, bool work) const
{
	return process_instance_key(address, state) + (work ? ":work" : ":idle");
}
void stage_process_costs(const CustomPropertyAddress &address, const ProcessState &state,
                         const ProcessDefinition &definition, bool work, std::int64_t actual_work, std::int64_t scale)
{
	const auto key = process_funding_key(address, state, work);
	if (rule_upkeep_plans.size() >= rule_instance_limit && !rule_upkeep_plans.contains(key))
		throw std::runtime_error("Process funding capacity exhausted");
	RuleUpkeepPlan plan;
	plan.context = process_context(address, state);
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
	if (const auto old = rule_upkeep_plans.find(key); old != rule_upkeep_plans.end())
	{
		if (old->second.quoted_work == actual_work && old->second.quoted_scale == scale)
			plan.maximum_output = old->second.maximum_output;
		if (old->second.rejected && !old->second.provisional_shortage)
			reject_rule_upkeep(plan, old->second.failure);
	}
	rule_upkeep_plans.insert_or_assign(key, std::move(plan));
	if (work && rule_upkeep_plans.at(key).rejected)
		reject_process_funding(address, state.id, rule_upkeep_plans.at(key).failure);
}
void append_process_fee_group(RuleUpkeepPlan &plan, const EffectGroupInvocation &initial)
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

void stage_process_upkeep(const CustomPropertyAddress &address, const ProcessState &state,
                          const ProcessDefinition &definition)
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
void stage_process_work(const CustomPropertyAddress &address, const ProcessState &state,
                        const ProcessDefinition &definition, std::int64_t actual_work, std::int64_t activity)
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
std::int64_t process_funding_output(const CustomPropertyAddress &address, const std::string &id, bool work) const
{
	const auto *state = process_state(address, id);
	if (!state)
		return 0;
	const auto found = rule_upkeep_plans.find(process_funding_key(address, *state, work));
	return found == rule_upkeep_plans.end() || found->second.rejected ? 0 : found->second.payment.output;
}
bool reject_process_funding(const CustomPropertyAddress &address, const std::string &id, const std::string &reason,
                            bool work = true)
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
void release_suspended_process_plans()
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
bool same_rule_work_input(const RuleWorkCredit &credit, const RuleUpkeepInput &input) const
{
	return credit.payer == input.payer && credit.store == input.store && credit.resource == input.resource &&
	       credit.property == input.property && credit.equipment == input.property_owner.equipment &&
	       credit.officer == input.property_owner.officer && credit.compartment == input.property_owner.compartment &&
	       credit.unit_work == input.unit_work;
}
std::int64_t rule_upkeep_credit(const RuleUpkeepPlan &plan, const RuleUpkeepInput &input) const
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
void resolve_rule_upkeep(const std::string &key, RuleUpkeepPlan &plan,
                         std::set<std::pair<CustomPropertyAddress, std::string>> *paid_stops = nullptr)
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
		auto ship = effect_group_target(actor.entity);
		if (!cost.equipment.empty() &&
		    !effect_group_equipment_available(actor.entity, effect_group_equipment(aliases, cost.equipment)))
			throw std::runtime_error("rules: upkeep equipment unavailable");
		if (!cost.property.empty())
		{
			auto property_actor = actor;
			if (!cost.equipment.empty())
			{
				property_actor.equipment = effect_group_equipment(aliases, cost.equipment);
				property_actor.officer.clear();
				property_actor.compartment = -1;
			}
			const auto address = custom_property_address(property_actor);
			const auto *state = custom_property_state(address);
			if (!state || !state->bases.contains(cost.property) ||
			    property_catalogue->at(cost.property).kind != CustomPropertyKind::Quantity)
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
		const auto resolved = named_stock(ship, cost.store);
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
				const auto resolved = named_stock(effect_group_target(cost.payer), cost.store);
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
const ResourceNetwork *process_producer_seed(EntityId id) const
{
	const auto found = process_producer_networks.find(id);
	return found == process_producer_networks.end() ? nullptr : &found->second;
}
void prepare_producer_exit_fees(PreparedEffectGroup &quote, const RuleUpkeepPlan &plan)
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
bool prepare_process_producer_funding()
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
void request_rule_upkeep()
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
			auto maximum = plan.maximum_output;
			if (!plan.depends_on.empty())
			{
				const auto dependency = rule_upkeep_plans.find(plan.depends_on);
				maximum = std::min(maximum, dependency == rule_upkeep_plans.end() || dependency->second.rejected ? 0
				                            : dependency->second.requested_output > 0 ? std::int64_t{1000}
				                                                                      : std::int64_t{0});
			}
			const auto bound = quote_rule_work(inputs, true, maximum);
			plan.requested_output = bound.output;
			if (bound.shortage && plan.shortage != "proportional")
			{
				reject_rule_upkeep(plan, "Required upkeep stock unavailable");
				plan.provisional_shortage = true;
				continue;
			}
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
					const auto outstanding = std::max(std::int64_t{0}, input.rate * bound.output - input.credit);
					const auto conversion = input.unit_work / resource_work_unit * 1000;
					const auto rate = outstanding / conversion + (outstanding % conversion != 0);
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
bool prepare_rule_upkeep_payments(std::map<EntityId, std::optional<ShipResourceAllocation>> &allocations,
                                  bool native_requests_removed = false)
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
			auto maximum = plan.maximum_output;
			if (!plan.depends_on.empty())
			{
				const auto dependency = rule_upkeep_plans.find(plan.depends_on);
				maximum = std::min(maximum, dependency == rule_upkeep_plans.end() || dependency->second.rejected ? 0
				                            : dependency->second.payment.output > 0 ? std::int64_t{1000}
				                                                                    : std::int64_t{0});
			}
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
				// A partially funded bundle must release the provisional quantity
				// reservation before subsequent plans are admitted. This bound only
				// decreases while the admitted request set remains fixed.
				plan.maximum_output = plan.payment.output;
				removed = true;
			}
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
bool commit_rule_upkeep_stocks()
{
	if (!rule_upkeep_effects)
		return false;
	return commit_effect_group(*rule_upkeep_effects).empty();
}
// The native allocation still owns its candidate network. Work operations and
// already quoted inputs join one prepared effect transaction before either can
// commit. A failed invocation identifies the process to withdraw and requote.
std::string append_rule_upkeep_effects(std::span<const EffectGroupInvocation> invocations,
                                       std::size_t &failed_invocation)
{
	if (!rule_upkeep_effects || rule_upkeep_effects->consumed)
		return "Upkeep quote unavailable";
	*rule_upkeep_effects = prepare_effect_groups(invocations, std::move(*rule_upkeep_effects));
	failed_invocation = rule_upkeep_effects->failed_invocation;
	return rule_upkeep_effects->failure;
}
void stage_rule_upkeep_process_receipt(const CustomPropertyAddress &address, const std::string &id,
                                       std::int64_t actual_work, std::int64_t idle, std::int64_t activity)
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
struct RuleProcessWork
{
	CustomPropertyAddress owner;
	std::string id;
	std::int64_t work = 0, activity = 1000;
};
bool prepare_process_funding_effects(std::span<const RuleProcessWork> completed,
                                     std::map<EntityId, std::optional<ShipResourceAllocation>> &allocations)
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
void finish_rule_upkeep()
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
		{
			for (std::size_t i = 0; i < plan.inputs.size(); ++i)
			{
				const auto &input = plan.inputs[i];
				auto credit = std::find_if(credits->begin(), credits->end(),
				                           [&](const auto &value)
				                           {
					                           return same_rule_work_input(value, input);
				                           });
				if (credit == credits->end())
					credits->push_back({input.payer, input.store, input.resource, plan.payment.credits[i],
					                    input.property, input.property_owner.equipment, input.property_owner.officer,
					                    input.property_owner.compartment, input.unit_work});
				else
					credit->prepaid = plan.payment.credits[i];
			}
		}
	}
}
