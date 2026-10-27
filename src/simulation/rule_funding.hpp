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
	std::int64_t maximum_output = 1000, requested_output = 1000;
	EntityId ability_ship = 0;
	std::string ability;
};
std::map<std::string, RuleUpkeepPlan> rule_upkeep_plans;
std::unique_ptr<StockTransaction> rule_upkeep_stocks;
std::set<EntityId> rule_upkeep_inventories;
std::map<CustomPropertyAddress, CustomPropertyState> rule_upkeep_properties, rule_upkeep_original_properties;

void clear_rule_upkeep()
{
	rule_upkeep_plans.clear();
	rule_upkeep_stocks.reset();
	rule_upkeep_inventories.clear();
	rule_upkeep_properties.clear();
	rule_upkeep_original_properties.clear();
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
bool same_rule_work_input(const RuleWorkCredit &credit, const RuleUpkeepInput &input) const
{
	return credit.payer == input.payer && credit.store == input.store && credit.resource == input.resource &&
	       credit.property == input.property && credit.equipment == input.property_owner.equipment &&
	       credit.officer == input.property_owner.officer && credit.compartment == input.property_owner.compartment;
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
	plan.failure = reason;
	plan.payment = {};
	plan.payment.output = 0;
	plan.payment.shortage = true;
}
void resolve_rule_upkeep(const std::string &key, RuleUpkeepPlan &plan)
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
		if (actor.ordnance || cost.amount < 0 || cost.amount > resource_limit)
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
			auto [it, inserted] = quantities.try_emplace(std::pair{address, cost.property}, input);
			if (!inserted)
			{
				if (it->second.rate > resource_limit - cost.amount)
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
			if (it->second.rate > resource_limit - cost.amount)
				throw std::runtime_error("rules: coalesced upkeep rate overflow");
			it->second.rate += cost.amount;
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
void request_rule_upkeep()
{
	StockTransaction raw;
	std::map<std::pair<CustomPropertyAddress, std::string>, std::int64_t> raw_quantities;
	// Rebuild only this mechanism's named requests. Native consumers retain their
	// requests, and every retry starts from authoritative uncommitted quantities.
	for (auto &[key, plan] : rule_upkeep_plans)
	{
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
		if (plan.rejected)
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
						const auto quantity =
						    std::get<std::int64_t>(
						        custom_property_state(input.property_owner)->bases.at(input.property)) -
						    property_catalogue->at(input.property).minimum;
						inputs.push_back(
						    {input.rate, input.credit, 0, raw_quantities.try_emplace(address, quantity).first->second});
					}
					else
					{
						auto &stock = named_stock(entities.at(input.payer), input.store).stock;
						inputs.push_back({input.rate, input.credit, 0, raw.view(stock).available(input.resource)});
					}
				}
			const auto bound = quote_rule_work(inputs, true, plan.maximum_output);
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
						auto &stock = named_stock(entities.at(input.payer), input.store).stock;
						if (!raw.consume(stock, input.resource, debit))
							throw std::logic_error("rules: raw upkeep request quote changed");
					}
				}
			for (const auto &input : plan.inputs)
				if (input.electrical != no_resource)
				{
					const auto outstanding = std::max(std::int64_t{0}, input.rate * bound.output - input.credit);
					const auto rate = outstanding / 1000 + (outstanding % 1000 != 0);
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
	rule_upkeep_stocks = std::make_unique<StockTransaction>();
	rule_upkeep_inventories.clear();
	rule_upkeep_properties.clear();
	rule_upkeep_original_properties.clear();
	bool removed = false;
	bool retired = native_requests_removed;
	for (auto &[key, plan] : rule_upkeep_plans)
	{
		if (plan.rejected)
			continue;
		std::vector<RuleWorkInput> inputs;
		std::vector<StockStore *> stocks;
		bool blocked = false;
		try
		{
			for (const auto &input : plan.inputs)
			{
				RuleWorkInput work{input.rate, input.credit};
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
					work.purchased = grant->second.work_funded;
					work.blocked = grant->second.blocked;
					blocked = blocked || work.blocked;
					stocks.push_back(nullptr);
				}
				else if (!input.property.empty())
				{
					const auto *state = custom_property_state(input.property_owner);
					if (!state || !state->bases.contains(input.property))
						throw std::runtime_error("rules: upkeep quantity owner unavailable");
					rule_upkeep_original_properties.try_emplace(input.property_owner, *state);
					auto &prepared = rule_upkeep_properties.try_emplace(input.property_owner, *state).first->second;
					work.available = std::get<std::int64_t>(prepared.bases.at(input.property)) -
					                 property_catalogue->at(input.property).minimum;
					stocks.push_back(nullptr);
				}
				else
				{
					auto resolved = named_stock(entities.at(input.payer), input.store,
					                            input.network ? &allocation->second->prepared_network() : nullptr);
					work.available = rule_upkeep_stocks->view(resolved.stock).available(input.resource);
					stocks.push_back(&resolved.stock);
				}
				inputs.push_back(work);
			}
			plan.payment = quote_rule_work(inputs, plan.shortage == "proportional", plan.maximum_output);
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
					auto &value = rule_upkeep_properties.at(input.property_owner).bases.at(input.property);
					value = std::get<std::int64_t>(value) - plan.payment.debits[i];
				}
				else if (stocks[i] && plan.payment.debits[i])
				{
					if (!rule_upkeep_stocks->consume(*stocks[i], plan.inputs[i].resource, plan.payment.debits[i]))
						throw std::logic_error("rules: prepared upkeep stock changed");
					if (!plan.inputs[i].network)
						rule_upkeep_inventories.insert(plan.inputs[i].payer);
				}
		}
		catch (const std::exception &error)
		{
			reject_rule_upkeep(plan, error.what());
			removed = true;
			retired = true;
			for (const auto &input : plan.inputs)
				if (input.electrical != no_resource && entities.contains(input.payer))
					removed = entities.at(input.payer).get_mut<ShipResources>().named_requests.erase(input.request) ||
					          removed;
		}
	}
	if (retired)
	{
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
	for (const auto &[address, original] : rule_upkeep_original_properties)
	{
		const auto *current = custom_property_state(address);
		if (!current || *current != original)
			return false;
	}
	if (!rule_upkeep_stocks || !rule_upkeep_stocks->commit())
		return false;
	begin_custom_property_batch();
	for (const auto &[address, prepared] : rule_upkeep_properties)
		commit_custom_property_state(address, prepared);
	finish_custom_property_batch();
	for (const auto id : rule_upkeep_inventories)
		if (auto *inventory = entities.at(id).try_get_mut<MaterialInventory>())
			++inventory->revision;
	return true;
}
void finish_rule_upkeep()
{
	const auto now = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	for (auto &[key, plan] : rule_upkeep_plans)
	{
		RuleExecutionState *state = plan.ability_ship ? nullptr : find_rule_execution(key);
		std::vector<RuleWorkCredit> *credits = nullptr;
		if (plan.ability_ship)
		{
			const auto ship = entities.find(plan.ability_ship);
			if (ship != entities.end())
				if (auto *runtime = ship->second.try_get_mut<AbilityRuntime>())
					if (const auto grant = runtime->grants.find(plan.ability); grant != runtime->grants.end())
						credits = &grant->second.credits;
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
					                    input.property_owner.compartment});
				else
					credit->prepaid = plan.payment.credits[i];
			}
		}
	}
}
