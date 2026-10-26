// Included inside World::Impl. These plans live for one resource phase only.
// Authoritative prepaid work and leases remain on their native rule owners.
struct RuleUpkeepInput
{
	EntityId payer = 0;
	std::string store, resource, request;
	std::int64_t rate = 0, credit = 0;
	ResourceIndex electrical = no_resource;
	bool network = false;
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
};
std::map<std::string, RuleUpkeepPlan> rule_upkeep_plans;
std::unique_ptr<StockTransaction> rule_upkeep_stocks;
std::set<EntityId> rule_upkeep_inventories;

void clear_rule_upkeep()
{
	rule_upkeep_plans.clear();
	rule_upkeep_stocks.reset();
	rule_upkeep_inventories.clear();
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
std::int64_t rule_upkeep_credit(const RuleUpkeepPlan &plan, EntityId payer, const std::string &store,
                                const std::string &resource) const
{
	const auto found =
	    std::find_if(plan.credits.begin(), plan.credits.end(),
	                 [&](const auto &credit)
	                 {
		                 return credit.payer == payer && credit.store == store && credit.resource == resource;
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
	std::sort(plan.inputs.begin(), plan.inputs.end(),
	          [](const auto &a, const auto &b)
	          {
		          return std::tie(a.payer, a.store, a.resource) < std::tie(b.payer, b.store, b.resource);
	          });
	std::size_t new_credits = 0;
	for (std::size_t i = 0; i < plan.inputs.size(); ++i)
	{
		auto &input = plan.inputs[i];
		input.credit = rule_upkeep_credit(plan, input.payer, input.store, input.resource);
		input.request = "rule:" + key + ":cost:" + std::to_string(i);
		if (input.request.size() > 512)
			throw std::runtime_error("rules: upkeep request identity exceeds supported bounds");
		if (std::none_of(plan.credits.begin(), plan.credits.end(),
		                 [&](const auto &value)
		                 {
			                 return value.payer == input.payer && value.store == input.store &&
			                        value.resource == input.resource;
		                 }))
			++new_credits;
	}
	if (plan.credits.size() + new_credits > 4096)
		throw std::runtime_error("rules: upkeep credit capacity exhausted");
}
void request_rule_upkeep()
{
	StockTransaction raw;
	for (auto &[key, plan] : rule_upkeep_plans)
	{
		try
		{
			resolve_rule_upkeep(key, plan);
			std::vector<RuleWorkInput> inputs;
			for (const auto &input : plan.inputs)
				if (input.electrical == no_resource)
				{
					auto &stock = named_stock(entities.at(input.payer), input.store).stock;
					inputs.push_back({input.rate, input.credit, 0, raw.view(stock).available(input.resource)});
				}
			const auto bound = quote_rule_work(inputs, true);
			if (bound.shortage && plan.shortage != "proportional")
			{
				reject_rule_upkeep(plan, "Required upkeep stock unavailable");
				continue;
			}
			std::size_t raw_index = 0;
			for (const auto &input : plan.inputs)
				if (input.electrical == no_resource)
				{
					auto &stock = named_stock(entities.at(input.payer), input.store).stock;
					if (!raw.consume(stock, input.resource, bound.debits[raw_index++]))
						throw std::logic_error("rules: raw upkeep request quote changed");
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
bool prepare_rule_upkeep_payments(std::map<EntityId, std::optional<ShipResourceAllocation>> &allocations)
{
	rule_upkeep_stocks = std::make_unique<StockTransaction>();
	rule_upkeep_inventories.clear();
	bool removed = false;
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
				else
				{
					auto resolved = named_stock(entities.at(input.payer), input.store,
					                            input.network ? &allocation->second->prepared_network() : nullptr);
					work.available = rule_upkeep_stocks->view(resolved.stock).available(input.resource);
					stocks.push_back(&resolved.stock);
				}
				inputs.push_back(work);
			}
			plan.payment = quote_rule_work(inputs, plan.shortage == "proportional");
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
			for (std::size_t i = 0; i < inputs.size(); ++i)
				if (stocks[i] && plan.payment.debits[i])
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
			for (const auto &input : plan.inputs)
				if (input.electrical != no_resource && entities.contains(input.payer))
					removed = entities.at(input.payer).get_mut<ShipResources>().named_requests.erase(input.request) ||
					          removed;
		}
	}
	return removed;
}
bool commit_rule_upkeep_stocks()
{
	if (!rule_upkeep_stocks || !rule_upkeep_stocks->commit())
		return false;
	for (const auto id : rule_upkeep_inventories)
		if (auto *inventory = entities.at(id).try_get_mut<MaterialInventory>())
			++inventory->revision;
	return true;
}
void finish_rule_upkeep()
{
	const auto now = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	for (auto &[key, plan] : rule_upkeep_plans)
		if (auto *state = find_rule_execution(key))
		{
			state->lease_tick = now + 1;
			state->lease_permille = plan.rejected ? 0 : plan.payment.output;
			state->blocker = plan.rejected ? plan.failure : plan.payment.shortage ? "Upkeep partially funded" : "";
			if (plan.rejected && plan.shortage == "stop")
				state->stop_tick = now + 1;
			if (plan.rejected)
				continue;
			for (std::size_t i = 0; i < plan.inputs.size(); ++i)
			{
				const auto &input = plan.inputs[i];
				auto credit = std::find_if(state->credits.begin(), state->credits.end(),
				                           [&](const auto &value)
				                           {
					                           return value.payer == input.payer && value.store == input.store &&
					                                  value.resource == input.resource;
				                           });
				if (credit == state->credits.end())
					state->credits.push_back({input.payer, input.store, input.resource, plan.payment.credits[i]});
				else
					credit->prepaid = plan.payment.credits[i];
			}
		}
}
