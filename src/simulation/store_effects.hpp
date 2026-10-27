// Included inside World::Impl. Resolve an explicit store identity across native
// mechanisms; there is no implicit ship-wide pool or remote supply lookup.
enum class NamedStockKind
{
	Physical,
	Electrical,
	Coolant
};
struct NamedStock
{
	StockStore &stock;
	std::string canonical;
	NamedStockKind kind;
	ResourceIndex index = no_resource;
};
NamedStock named_stock(flecs::entity ship, const std::string &name, ResourceNetwork *prepared_network = nullptr) const
{
	StockStore *result = nullptr;
	std::string canonical;
	NamedStockKind kind = NamedStockKind::Physical;
	ResourceIndex index = no_resource;
	const auto accept = [&](StockStore &stock, const std::string &key, NamedStockKind candidate_kind,
	                        ResourceIndex candidate_index = no_resource,
	                        const std::string &candidate_canonical = std::string{})
	{
		if (name != key && name != stock.definition().id)
			return;
		if (result && result != &stock)
			throw std::runtime_error("Ambiguous store identity: " + name);
		result = &stock;
		canonical = candidate_canonical.empty() ? key : candidate_canonical;
		kind = candidate_kind;
		index = candidate_index;
	};
	if (auto *physical = ship.try_get_mut<ShipStockStores>())
		for (auto &[key, stock] : physical->stores)
			accept(stock, key, NamedStockKind::Physical);
	if (auto *physical = ship.try_get_mut<ShipStockStores>())
		if (const auto *inventory = ship.try_get<MaterialInventory>())
		{
			const auto &stores = definitions.at(ship.get<Definition>().name).logistics.stores;
			for (std::size_t i = 0; i < stores.size(); ++i)
				if (name == stores[i].id)
					accept(physical->stores.at(inventory->stores.at(i).stock), name, NamedStockKind::Physical,
					       no_resource, inventory->stores.at(i).stock);
		}
	if (auto *power = ship.try_get_mut<ShipResources>())
	{
		auto &network = prepared_network ? *prepared_network : power->network;
		for (ResourceIndex i = 0; i < network.stores().size(); ++i)
			accept(network.stock(i), "energy:" + network.definition().stores[i].id, NamedStockKind::Electrical, i);
		for (ResourceIndex i = 0; i < network.coolant_sources().size(); ++i)
			accept(network.coolant_stock(i), "coolant:" + network.definition().coolant_sources[i].id,
			       NamedStockKind::Coolant, i);
	}
	if (!result)
		throw std::runtime_error("Unknown store: " + name);
	return {*result, canonical, kind, index};
}

// This epoch is transient quote ownership, not a gameplay counter or receipt.
std::uint64_t effect_commit_epoch = 1;
NativeRuleContext effect_invocation_context(const EffectGroupInvocation &frame, std::int64_t tick) const
{
	if (frame.rule_context)
		return *frame.rule_context;
	NativeRuleContext context;
	context.owner = context.subject = context.target = {frame.protected_ship};
	context.source =
	    context.instigator = {frame.attacker ? frame.attacker : frame.protected_ship, frame.original_equipment};
	context.hit = frame.hit_context.value_or(rule_hit_context(context.subject));
	context.tick = tick;
	return context;
}
RuleActor effect_group_actor(const EffectGroupInvocation &frame, const std::string &role) const
{
	if (frame.rule_context)
		return rule_actor(*frame.rule_context, role);
	if (role == "protected")
		return {frame.protected_ship,
		        {},
		        {},
		        frame.hit_context && frame.hit_context->target == frame.protected_ship && frame.hit_context->ordnance};
	if (role == "attacker")
		return {frame.attacker, frame.original_equipment, {}};
	// Legacy effect groups have two explicit entity arguments. New rule adapters
	// supply the richer frozen roles instead of deriving them from provenance text.
	if (role == "owner" || role == "subject" || role == "target")
		return effect_group_actor(frame, "protected");
	if (role == "source" || role == "instigator")
		return {frame.attacker, frame.original_equipment, {}};
	throw std::runtime_error("Unknown effect target role");
}
std::string effect_group_equipment(const EffectGroupInvocation &frame, const std::string &binding) const
{
	if (binding.empty() || binding.front() != '$')
		return binding;
	if (binding == "$hit")
	{
		if (!frame.hit_context || !payload_context_available(*frame.hit_context))
			throw std::runtime_error("Required hit equipment unavailable");
		const auto &hit = *frame.hit_context;
		auto equipment = hit.kind == PayloadOutcomeKind::Shield
		                     ? "effect:" + std::to_string(hit.contribution) + ":" + hit.contribution_name
		                     : hit.equipment;
		if (equipment.empty())
			throw std::runtime_error("Hit does not identify equipment");
		return equipment;
	}
	const auto actor = effect_group_actor(frame, binding.substr(1));
	if (actor.kind == PayloadOutcomeKind::Shield && actor.contribution && !actor.contribution_name.empty())
		return "effect:" + std::to_string(actor.contribution) + ":" + actor.contribution_name;
	if (actor.equipment.empty())
		throw std::runtime_error("Required equipment binding unavailable");
	return actor.equipment;
}
flecs::entity effect_group_target(EntityId id, bool ordnance = false) const
{
	if (ordnance)
	{
		PayloadHitContext context{id};
		context.ordnance = true;
		const auto target = payload_target(context);
		if (!target || !payload_context_available(context))
			throw std::runtime_error("Required ordnance target unavailable");
		return target;
	}
	const auto at = entities.find(id);
	if (at == entities.end() || combat_dead(at->second))
		throw std::runtime_error("Required target unavailable");
	return at->second;
}
bool effect_group_equipment_available(EntityId ship, const std::string &equipment) const
{
	if (!entities.contains(ship))
		return false;
	const auto entity = entities.at(ship);
	if (equipment.starts_with("effect:"))
	{
		const auto *shields = entity.try_get<ShieldComponent>();
		return shields && std::any_of(shields->contributions.begin(), shields->contributions.end(),
		                              [&](const auto &grant)
		                              {
			                              return equipment == "effect:" + std::to_string(grant.effect_id) + ":" +
			                                                      grant.definition.name &&
			                                     grant.operating;
		                              });
	}
	const auto fitted = instance(entity, equipment);
	return fitted && fitted->entity.get<Equipment>().enabled && !structurally_disabled(entity, equipment) &&
	       fault_effectiveness(entity, equipment) > 0;
}
PropertyEffects &prepare_property_owner(PreparedEffectGroup &quote, flecs::entity ship) const
{
	const auto id = ship.get<StableId>().id;
	auto original = quote.original_properties.find(id);
	if (original == quote.original_properties.end())
		original = quote.original_properties.emplace(id, read_property_effects(ship)).first;
	return quote.properties.try_emplace(id, original->second).first->second;
}
std::vector<ActiveRuleEffect> &prepare_active_rule_owner(PreparedEffectGroup &quote) const
{
	if (!quote.active_effects)
	{
		quote.original_active_effects = active_rule_snapshot();
		quote.active_effects = *quote.original_active_effects;
	}
	return *quote.active_effects;
}
void prepare_group_modifier(PreparedEffectGroup &quote, const EffectGroupInvocation &frame, flecs::entity ship,
                            const EffectOperation &operation, const std::string &equipment)
{
	if (!ship.has<PropertyEffectOwner>() || !ship.has<StableId>())
		throw std::runtime_error("Incompatible modifier owner");
	const auto id = ship.get<StableId>().id;
	auto compiled = modifier_templates.at(operation.effect);
	const auto role = operation.role == "$role" ? frame.rule_context ? frame.rule_context->property_role : std::string{}
	                                            : operation.role;
	if (operation.role == "$role" && role.empty())
		throw std::runtime_error("Required modifier role unavailable");
	if (!role.empty())
		std::erase_if(compiled.contributions,
		              [&](const auto &contribution)
		              {
			              return contribution.role != role;
		              });
	if (frame.rule_context && !frame.rule_context->property_filter.empty())
		std::erase_if(compiled.contributions,
		              [&](const auto &contribution)
		              {
			              return contribution.property != frame.rule_context->property_filter;
		              });
	if (compiled.contributions.empty() || !modifier_target_compatible(ship, equipment, compiled))
		throw std::runtime_error("Incompatible modifier role/target");
	for (auto &contribution : compiled.contributions)
	{
		const auto delta = operation.delta_permille.value_or(contribution.delta_permille);
		if (delta < -100000 || delta > 100000)
			throw std::runtime_error("Modifier delta outside supported bounds");
		contribution.delta_permille = delta * frame.output_permille / 1000;
	}
	const auto source = frame.source + "." + operation.grant;
	validate_effect_identity(source, equipment, false);
	auto &prepared = prepare_property_owner(quote, ship);
	const auto existing = std::find_if(prepared.applications.begin(), prepared.applications.end(),
	                                   [&](const auto &application)
	                                   {
		                                   return application.source == source && application.equipment == equipment;
	                                   });
	if (existing != prepared.applications.end() && existing->definition != compiled.id)
		throw std::runtime_error("Modifier grant name belongs to another definition");
	if (existing == prepared.applications.end() || !frame.maintained)
		install_modifier_grant(prepared, compiled, source, equipment, quote.tick, frame.maintained);
	for (auto &application : prepared.applications)
		if (application.source == source && application.equipment == equipment)
			for (auto &contribution : application.modifier.values)
				for (const auto &value : compiled.contributions)
					if (contribution.name == value.name &&
					    (contribution.custom.empty() ? contribution.property == property_key(value.property)
					                                 : contribution.custom == value.property))
						contribution.delta = value.delta_permille;
}
flecs::entity watch_effect_target(PreparedEffectGroup &quote, const EffectGroupInvocation &frame, EntityId id,
                                  bool outcome = true, bool ordnance = false) const
{
	// Guards carry their own ordnance identity; a later batch frame must not
	// reinterpret an earlier frame's same-number physical/ordnance target.
	ordnance = ordnance || (!frame.rule_context && outcome && frame.hit_context && frame.hit_context->target == id &&
	                        frame.hit_context->ordnance);
	quote.targets.push_back({id, outcome, ordnance});
	return effect_group_target(id, ordnance);
}
StockStore &prepared_effect_stock(PreparedEffectGroup &quote, const EffectGroupInvocation &frame, EntityId id,
                                  const std::string &name, bool outcome = true)
{
	auto resolved = named_stock(watch_effect_target(quote, frame, id, outcome), name,
	                            quote.payments.contains(id) ? &quote.payments.at(id).network : nullptr);
	if (resolved.kind == NamedStockKind::Physical)
		quote.changed_inventories.insert(id);
	return resolved.stock;
}
CustomPropertyState &prepare_custom_property_owner(PreparedEffectGroup &quote, const CustomPropertyAddress &address)
{
	const auto *current = custom_property_state(address);
	if (!current)
		throw std::runtime_error("Custom property owner unavailable");
	quote.original_custom_properties.try_emplace(address, *current);
	return quote.custom_properties.try_emplace(address, *current).first->second;
}
RuleActor effect_property_actor(const EffectGroupInvocation &frame, const RuleActor &original,
                                const std::string &equipment, const std::string &officer,
                                const std::string &compartment = {}) const
{
	if ((!equipment.empty() + !officer.empty() + !compartment.empty()) > 1)
		throw std::runtime_error("Effect owner selectors conflict");
	auto actor = equipment.empty() && officer.empty() && compartment.empty() ? original : rule_body_actor(original);
	if (!equipment.empty())
	{
		actor.equipment = effect_group_equipment(frame, equipment);
		actor.officer.clear();
		actor.compartment = -1;
	}
	if (!officer.empty())
	{
		actor.officer = officer;
		actor.equipment.clear();
		actor.compartment = -1;
	}
	if (!compartment.empty())
	{
		const auto &combat = definition(actor.entity).combat;
		if (!combat || !combat->compartment_ids.contains(compartment))
			throw std::runtime_error("Effect compartment unavailable");
		actor = rule_body_actor(actor);
		actor.kind = PayloadOutcomeKind::Compartment;
		actor.compartment = combat->compartment_ids.at(compartment);
	}
	return actor;
}
void prepare_custom_property_operation(PreparedEffectGroup &quote, const EffectGroupInvocation &frame,
                                       const EffectOperation &op, const RuleActor &target, std::size_t index)
{
	const auto address = custom_property_address(effect_property_actor(frame, target, op.equipment, op.officer));
	auto &state = prepare_custom_property_owner(quote, address);
	const auto found = state.bases.find(op.property);
	if (found == state.bases.end())
		throw std::runtime_error("Custom property is not declared on target");
	const auto &definition = property_catalogue->at(op.property);
	if (op.kind == EffectOperationKind::PropertySet)
	{
		read_custom_property_value(definition, custom_property_json(op.value), "property operation");
		found->second = op.value;
		return;
	}
	if (!std::holds_alternative<std::int64_t>(found->second))
		throw std::runtime_error("Numeric property required");
	auto amount = op.amount;
	if (op.kind == EffectOperationKind::PropertySubtract)
		amount = -amount;
	if (op.kind == EffectOperationKind::PropertyRate)
	{
		const auto key = custom_property_rate_key(frame, index, op.property);
		const auto previous = key && state.remainders.contains(*key) ? state.remainders.at(*key) : 0;
		const auto numerator = amount + previous;
		amount = numerator / rate;
		const auto remainder = numerator % rate;
		if (key && remainder)
		{
			if (key->size() > 512 ||
			    (!state.remainders.contains(*key) && state.remainders.size() >= custom_property_rate_limit))
				throw std::runtime_error("Property rate identity capacity exhausted");
			state.remainders[*key] = remainder;
		}
		else if (key)
			state.remainders.erase(*key);
	}
	const auto base = std::get<std::int64_t>(found->second);
	const auto next = base + amount;
	if (!op.saturate && (next < definition.minimum || next > definition.maximum))
		throw std::runtime_error("Property operation exceeds declared bounds");
	found->second = std::clamp(next, definition.minimum, definition.maximum);
}
struct PreparedEffectCost
{
	EntityId payer = 0;
	std::string store, resource;
	std::int64_t amount = 0;
	ResourceIndex energy_store = no_resource;
	std::optional<CustomPropertyAddress> property_owner;
	std::string property;
};
std::vector<PreparedEffectCost> resolve_effect_costs(PreparedEffectGroup &quote, const EffectGroupInvocation &frame)
{
	const auto &execution = frame.execution;
	const auto protected_ship = frame.protected_ship;
	const auto owner = [&](EntityId id, bool outcome = true, bool ordnance = false)
	{
		return watch_effect_target(quote, frame, id, outcome, ordnance);
	};
	std::vector<PreparedEffectCost> costs;
	if (frame.output_permille < 0 || frame.output_permille > 1000)
		throw std::runtime_error("Effect output outside supported bounds");
	validate_effect_identity(frame.source, {}, false);
	if (execution.energy)
	{
		const auto actor = effect_group_actor(frame, "protected");
		auto payer = owner(protected_ship, false, actor.ordnance);
		const auto equipment = effect_group_equipment(frame, execution.payer);
		const auto fitted = instance(payer, equipment);
		if (!payer.has<ShipResources>() || !fitted || !fitted->entity.has<ResourceBinding>() ||
		    !effect_group_equipment_available(protected_ship, equipment))
			throw std::runtime_error("Payer equipment unavailable");
		quote.equipment.emplace_back(protected_ship, equipment);
		const auto &power = payer.get<ShipResources>();
		const auto index = power.definition.systems.at(fitted->entity.get<ResourceBinding>().slot).store;
		if (index >= power.network.stores().size())
			throw std::runtime_error("Energy payer store unavailable");
		const auto &definition = power.network.definition().stores[index];
		costs.push_back({protected_ship, "energy:" + definition.id, definition.resource, execution.energy, index});
	}
	for (const auto &cost : execution.costs)
	{
		const auto actor = effect_group_actor(frame, cost.payer);
		auto payer = owner(actor.entity, false, actor.ordnance);
		if (cost.amount < 0 || cost.amount > resource_limit)
			throw std::runtime_error("Effect cost outside supported bounds");
		if (!cost.equipment.empty())
		{
			const auto equipment = effect_group_equipment(frame, cost.equipment);
			if (!effect_group_equipment_available(actor.entity, equipment))
				throw std::runtime_error("Cost equipment unavailable");
			quote.equipment.emplace_back(actor.entity, equipment);
		}
		if (!cost.property.empty())
		{
			const auto address = custom_property_address(effect_property_actor(frame, actor, cost.equipment, {}));
			auto &state = prepare_custom_property_owner(quote, address);
			const auto value = state.bases.find(cost.property);
			if (value == state.bases.end() ||
			    property_catalogue->at(cost.property).kind != CustomPropertyKind::Quantity)
				throw std::runtime_error("Quantity cost property unavailable");
			PreparedEffectCost input;
			input.payer = actor.entity;
			input.amount = cost.amount;
			input.property_owner = address;
			input.property = cost.property;
			costs.push_back(std::move(input));
			continue;
		}
		const auto resolved = named_stock(payer, cost.store);
		const auto native = resolved.kind == NamedStockKind::Electrical ? resolved.index : no_resource;
		if (native != no_resource &&
		    payer.get<ShipResources>().network.definition().stores[native].resource != cost.resource)
			throw std::runtime_error("Cost resource does not match native reserve");
		costs.push_back({actor.entity, resolved.canonical, cost.resource, cost.amount, native});
	}
	return costs;
}
bool spend_prepared_effect_energy(PreparedEffectGroup &quote, const PreparedEffectCost &cost)
{
	auto &network = quote.payments.at(cost.payer).network;
	auto &reserve = network.stock(cost.energy_store);
	// Native budgets and heat must see all preceding staged stock work, including
	// earlier independent frames. Only this quoted network is temporarily loaded.
	auto staged = quote.stocks.view(reserve);
	reserve.swap(staged);
	bool paid = false;
	try
	{
		paid = network.spend_external(cost.energy_store, cost.amount);
	}
	catch (...)
	{
		reserve.swap(staged);
		throw;
	}
	reserve.swap(staged);
	return paid && quote.stocks.consume(reserve, cost.resource, cost.amount);
}
void prepare_owner_effect_removal(PreparedEffectGroup &quote, const EffectGroupInvocation &frame,
                                  const EffectOperation &operation)
{
	if (operation.kind != EffectOperationKind::Remove || operation.target != "owner" || !operation.grant.empty() ||
	    !operation.effect.empty() || !operation.equipment.empty() || !operation.officer.empty() ||
	    operation.all_sources || !frame.rule_context || frame.rule_context->owner_grant.empty() ||
	    !frame.rule_context->owner_effect_id || frame.rule_context->owner_effect_kind == NativeEffectKind::None)
		throw std::runtime_error("Owner removal requires an exact native grant context");
	const auto &context = *frame.rule_context;
	const auto &actor = context.owner;
	const auto &source = context.owner_grant;
	const auto identity = context.owner_effect_id;
	auto ship = watch_effect_target(quote, frame, actor.entity, false, actor.ordnance);
	bool removed = false;
	if (!actor.ordnance && context.owner_effect_kind == NativeEffectKind::Shield)
	{
		const auto *current =
		    quote.shields.contains(actor.entity) ? &quote.shields.at(actor.entity) : ship.try_get<ShieldComponent>();
		const auto matches = [&](const auto &contribution)
		{
			return contribution.effect_id == identity && contribution.source == source;
		};
		if (current && std::any_of(current->contributions.begin(), current->contributions.end(), matches))
		{
			quote.original_shields.try_emplace(actor.entity, ship.get<ShieldComponent>());
			auto &prepared = quote.shields.try_emplace(actor.entity, *current).first->second;
			removed = std::erase_if(prepared.contributions, matches) > 0;
		}
	}
	else if (!actor.ordnance && context.owner_effect_kind == NativeEffectKind::Modifier)
	{
		auto &prepared = prepare_property_owner(quote, ship);
		removed = std::erase_if(prepared.applications,
		                        [&](const auto &application)
		                        {
			                        return application.id == identity && application.source == source &&
			                               application.equipment == actor.equipment;
		                        }) > 0;
	}
	else if (!actor.ordnance && context.owner_effect_kind == NativeEffectKind::Ability)
	{
		const auto *current =
		    quote.abilities.contains(actor.entity) ? &quote.abilities.at(actor.entity) : ship.try_get<AbilityRuntime>();
		const auto matches = [&](const auto &entry)
		{
			return entry.second.generation == identity && entry.second.parent == source;
		};
		if (current && std::any_of(current->grants.begin(), current->grants.end(), matches))
		{
			quote.original_abilities.try_emplace(actor.entity, ship.get<AbilityRuntime>());
			auto &prepared = quote.abilities.try_emplace(actor.entity, *current).first->second;
			for (auto it = prepared.grants.begin(); it != prepared.grants.end(); ++it)
				if (it->second.generation == identity && it->second.parent == source)
				{
					stop_ability(actor.entity, prepared, it->second);
					prepared.grants.erase(it);
					removed = true;
					break;
				}
		}
	}
	else if (context.owner_effect_kind == NativeEffectKind::Process)
	{
		const auto address = custom_property_address(actor);
		auto &runtime = prepare_process_owner(quote, address);
		for (auto it = runtime.processes.begin(); it != runtime.processes.end(); ++it)
			if (it->second.generation == identity &&
			    (process_run_source(address, it->second) == source || it->second.source == source))
			{
				if (!it->second.source.empty())
					runtime.processes.erase(it);
				else
				{
					it->second.admitted = it->second.requested = false;
					it->second.phase = "off";
					it->second.idle_output = it->second.work_output = it->second.actual_work = 0;
					it->second.idle_credits.clear();
					it->second.work_credits.clear();
					it->second.work_remainder = 0;
				}
				removed = true;
				break;
			}
	}
	else if (context.owner_effect_kind == NativeEffectKind::Periodic)
	{
		const ActiveRuleEffect *current = nullptr;
		if (quote.active_effects)
		{
			const auto at = std::find_if(quote.active_effects->begin(), quote.active_effects->end(),
			                             [&](const auto &effect)
			                             {
				                             return effect.id == identity;
			                             });
			if (at != quote.active_effects->end())
				current = &*at;
		}
		else if (const auto entity = active_rule_entity(identity))
			current = &entity.get<ActiveRuleEffect>();
		const auto matches = [&](const auto &effect)
		{
			return effect.id == identity && effect.source == source && effect.context.target == actor.entity &&
			       effect.context.ordnance == actor.ordnance;
		};
		if (current && matches(*current))
			removed = std::erase_if(prepare_active_rule_owner(quote), matches) > 0;
	}
	if (!removed)
		throw std::runtime_error("Owner native grant is no longer available");
}
void stage_effect_costs(PreparedEffectGroup &quote, const EffectGroupInvocation &frame,
                        std::span<const PreparedEffectCost> costs)
{
	for (const auto &cost : costs)
	{
		if (!cost.amount)
			continue;
		if (cost.property_owner)
		{
			auto &value = prepare_custom_property_owner(quote, *cost.property_owner).bases.at(cost.property);
			auto &quantity = std::get<std::int64_t>(value);
			if (quantity - property_catalogue->at(cost.property).minimum < cost.amount)
				throw std::runtime_error("Required quantity payment unavailable");
			quantity -= cost.amount;
		}
		else if (cost.energy_store != no_resource)
		{
			bool paid = false;
			if (quote.pre_generation)
			{
				auto &network = quote.payments.at(cost.payer).network;
				auto &reserve = network.stock(cost.energy_store);
				const auto reserved =
				    reserve.quantity(cost.resource) - quote.stocks.view(reserve).quantity(cost.resource);
				paid = network.producer_input_operational(cost.energy_store) &&
				       cost.amount <=
				           std::max<std::int64_t>(0, network.producer_input_budget(cost.energy_store) - reserved) &&
				       quote.stocks.consume(reserve, cost.resource, cost.amount);
			}
			else
				paid = spend_prepared_effect_energy(quote, cost);
			if (!paid)
				throw std::runtime_error("Energy payment unavailable");
		}
		else if (!quote.stocks.consume(prepared_effect_stock(quote, frame, cost.payer, cost.store, false),
		                               cost.resource, cost.amount))
			throw std::runtime_error("Required resource payment unavailable");
	}
}
void stage_effect_invocation(PreparedEffectGroup &quote, std::size_t invocation)
{
	const auto &frame = quote.invocations.at(invocation);
	const auto &execution = frame.execution;
	const auto protected_ship = frame.protected_ship;
	const auto attacker = frame.attacker;
	const auto &source = frame.source;
	const auto &original_equipment = frame.original_equipment;
	const auto owner = [&](EntityId id, bool outcome = true, bool ordnance = false)
	{
		return watch_effect_target(quote, frame, id, outcome, ordnance);
	};
	const auto stock = [&](EntityId id, const std::string &name, bool outcome = true) -> StockStore &
	{
		return prepared_effect_stock(quote, frame, id, name, outcome);
	};
	std::size_t operation_index = 0;
	for (const auto &operation : execution.operations)
	{
		const auto current_operation = operation_index++;
		if (operation.scope == "owner")
		{
			prepare_owner_effect_removal(quote, frame, operation);
			continue;
		}
		auto op = operation;
		op.equipment = effect_group_equipment(frame, op.equipment);
		const auto actor = effect_group_actor(frame, op.target);
		const auto id = actor.entity;
		const bool outcome = frame.rule_context
		                         ? actor.kind != PayloadOutcomeKind::Entity || actor.ordnance
		                         : frame.hit_context && id == frame.hit_context->target &&
		                               actor.ordnance == frame.hit_context->ordnance && op.target != "attacker" &&
		                               op.target != "source" && op.target != "instigator";
		PayloadHitContext address{id};
		address.kind = actor.kind;
		address.ordnance = actor.ordnance;
		if (actor.kind == PayloadOutcomeKind::Compartment || actor.kind == PayloadOutcomeKind::System)
			address.compartment = actor.compartment;
		if (actor.kind == PayloadOutcomeKind::System)
			address.equipment = actor.equipment;
		if (actor.kind == PayloadOutcomeKind::Shield)
		{
			address.contribution = actor.contribution;
			address.contribution_name = actor.contribution_name;
		}
		if (!frame.rule_context && outcome)
			address = *frame.hit_context;
		auto ship = owner(id, outcome, actor.ordnance);
		const auto *ability_state =
		    quote.abilities.contains(id) ? &quote.abilities.at(id) : ship.try_get<AbilityRuntime>();
		const auto ability_owner = op.officer.empty() ? op.equipment : op.officer;
		const auto ability_source = op.officer.empty() ? "equipment" : "officer";
		const auto ability_grant_id =
		    "temporary:" + source + "." + op.grant + ":" + ability_source + ":" + ability_owner;
		const bool removing_ability =
		    op.kind == EffectOperationKind::Remove && ability_state && ability_state->grants.contains(ability_grant_id);
		std::optional<CustomPropertyAddress> process_address;
		if (!actor.ordnance &&
		    (actor.kind != PayloadOutcomeKind::Shield || !op.equipment.empty() || !op.officer.empty() ||
		     !op.compartment.empty()) &&
		    (process_grant_templates.contains(op.effect) || op.kind == EffectOperationKind::Remove))
		{
			const auto candidate =
			    custom_property_address(effect_property_actor(frame, actor, op.equipment, op.officer, op.compartment));
			const auto *runtime =
			    quote.processes.contains(candidate) ? &quote.processes.at(candidate) : process_runtime(candidate);
			if (process_grant_templates.contains(op.effect) ||
			    (runtime && std::any_of(runtime->processes.begin(), runtime->processes.end(),
			                            [&](const auto &entry)
			                            {
				                            return entry.second.source == source + "." + op.grant;
			                            })))
				process_address = candidate;
		}
		if (op.kind == EffectOperationKind::HeatAdd || op.kind == EffectOperationKind::HeatRemove)
		{
			prepare_thermal_operation(quote, frame, op);
		}
		else if (op.kind >= EffectOperationKind::PropertySet && op.kind <= EffectOperationKind::PropertyRate)
		{
			prepare_custom_property_operation(quote, frame, op, actor, current_operation);
		}
		else if (op.kind == EffectOperationKind::Damage)
		{
			if (op.damage < 0 || op.damage > 1000000)
				throw std::runtime_error("Effect damage outside supported bounds");
			if (outcome)
			{
				if (!payload_context_available(address))
					throw std::runtime_error("Damage outcome unavailable");
			}
			else if (!ship.has<CombatStructure>() && !ship.has<Destructible>())
				throw std::runtime_error("Damage target incompatible");
			if (op.damage)
				quote.damages.push_back(
				    {id, op.damage, outcome || actor.ordnance || !ship.has<CombatStructure>(), address, invocation});
		}
		else if (op.kind >= EffectOperationKind::StoreAdd)
		{
			if (actor.ordnance)
				throw std::runtime_error("Ordnance does not own a stock store");
			auto &destination = stock(id, op.store, outcome);
			bool valid = false;
			switch (op.kind)
			{
			case EffectOperationKind::StoreAdd:
				valid = quote.stocks.add(destination, op.resource, op.amount);
				break;
			case EffectOperationKind::StoreConsume:
				valid = quote.stocks.consume(destination, op.resource, op.amount);
				break;
			case EffectOperationKind::StoreSet:
				valid = quote.stocks.set(destination, op.resource, op.amount);
				break;
			case EffectOperationKind::StoreFill:
				valid = quote.stocks.fill(destination, op.resource);
				break;
			case EffectOperationKind::StoreTransfer:
				valid = quote.stocks.transfer(
				    stock(id == attacker ? protected_ship : attacker, op.from_store, id == attacker), destination,
				    op.resource, op.amount);
				break;
			default:
				break;
			}
			if (!valid)
				throw std::runtime_error("Store operation cannot satisfy exact stock/capacity commitment");
		}
		else if (process_address)
		{
			auto &runtime = prepare_process_owner(quote, *process_address);
			if (op.kind == EffectOperationKind::Remove)
				remove_process_grant(runtime, source + "." + op.grant);
			else
			{
				auto grant = process_grant_templates.at(op.effect);
				grant.native_id = validate_process_grant(*process_address, grant);
				install_process_grant(runtime, grant, op.effect, source + "." + op.grant, op.grant, quote.tick,
				                      effect_invocation_context(frame, quote.tick));
			}
		}
		else if (ability_grant_templates.contains(op.effect) || removing_ability)
		{
			if (!ship.has<AbilityRuntime>())
				throw std::runtime_error("Incompatible ability grant target");
			quote.original_abilities.try_emplace(id, ship.get<AbilityRuntime>());
			auto &prepared = quote.abilities.try_emplace(id, ship.get<AbilityRuntime>()).first->second;
			prepare_ability_grant(prepared, id, op, source + "." + op.grant);
		}
		else if (periodic_templates.contains(op.effect) ||
		         (op.kind == EffectOperationKind::Remove &&
		          active_rule_effect_exists(quote.active_effects ? &*quote.active_effects : nullptr,
		                                    source + "." + op.grant, id)))
		{
			const auto context = address;
			auto &active = prepare_active_rule_owner(quote);
			prepare_active_rule_effect(active, quote.next_active, op, source + "." + op.grant, attacker, context,
			                           source, original_equipment, frame.rule_context ? &*frame.rule_context : nullptr);
		}
		else if (op.kind == EffectOperationKind::Apply && modifier_templates.contains(op.effect))
			prepare_group_modifier(quote, frame, ship, op,
			                       op.equipment.empty()
			                           ? custom_modifier_target(effect_property_actor(frame, actor, {}, op.officer))
			                           : op.equipment);
		else
		{
			const auto token = op.equipment.empty()
			                       ? custom_modifier_target(effect_property_actor(frame, actor, {}, op.officer))
			                       : op.equipment;
			const auto &current_properties = quote.properties.contains(id)     ? quote.properties.at(id)
			                                 : ship.has<PropertyEffectOwner>() ? read_property_effects(ship)
			                                                                   : PropertyEffects{};
			const bool logical_modifier =
			    op.kind == EffectOperationKind::Remove &&
			    std::any_of(current_properties.applications.begin(), current_properties.applications.end(),
			                [&](const auto &application)
			                {
				                return application.source == source + "." + op.grant && application.equipment == token;
			                });
			if (logical_modifier || (op.kind == EffectOperationKind::Remove && !op.officer.empty()))
				op.equipment = token;
			const auto kind = (!op.equipment.empty() || logical_modifier) ? EffectApplicationKind::Modifier
			                                                              : EffectApplicationKind::Shield;
			ShieldComponent *shield = nullptr;
			PropertyEffects *property = nullptr;
			if (kind == EffectApplicationKind::Shield)
			{
				if (!ship.has<ShieldComponent>())
					throw std::runtime_error("Incompatible shield target");
				quote.original_shields.try_emplace(id, ship.get<ShieldComponent>());
				shield = &quote.shields.try_emplace(id, ship.get<ShieldComponent>()).first->second;
			}
			else
			{
				if (!ship.has<PropertyEffectOwner>())
					throw std::runtime_error("Incompatible modifier target");
				property = &prepare_property_owner(quote, ship);
			}
			prepare_effect_application(ship,
			                           {kind,
			                            op.kind == EffectOperationKind::Remove ? EffectApplicationOperation::Remove
			                                                                   : EffectApplicationOperation::Apply,
			                            source + "." + op.grant, op.equipment, op.effect},
			                           shield, property, quote.tick);
		}
	}
}
ProcessRuntime &prepare_process_owner(PreparedEffectGroup &quote, const CustomPropertyAddress &address) const
{
	const auto *runtime = process_runtime(address);
	if (!runtime)
		throw std::runtime_error("Process owner unavailable");
	quote.original_processes.try_emplace(address, *runtime);
	return quote.processes.try_emplace(address, *runtime).first->second;
}
std::vector<EffectGroupInvocation> process_removal_frames(PreparedEffectGroup &quote,
                                                          const EffectGroupInvocation &frame)
{
	std::vector<EffectGroupInvocation> result;
	for (const auto &operation : frame.execution.operations)
	{
		if (operation.kind != EffectOperationKind::Remove || operation.all_sources)
			continue;
		const bool owner_removal = operation.scope == "owner" && frame.rule_context &&
		                           frame.rule_context->owner_effect_kind == NativeEffectKind::Process;
		if (operation.scope != "local" && !owner_removal)
			continue;
		const auto actor = effect_group_actor(frame, operation.target);
		if (actor.ordnance || actor.kind == PayloadOutcomeKind::Shield)
			continue;
		const auto address = custom_property_address(
		    effect_property_actor(frame, actor, operation.equipment, operation.officer, operation.compartment));
		const auto *runtime = process_runtime(address);
		if (!runtime)
			continue;
		const auto source = owner_removal ? frame.rule_context->owner_grant : frame.source + "." + operation.grant;
		for (const auto &[id, state] : runtime->processes)
			if ((state.source == source || (owner_removal && state.generation == frame.rule_context->owner_effect_id &&
			                                process_run_source(address, state) == source)) &&
			    state.admitted && quote.process_stops.emplace(address, process_run_source(address, state)).second)
			{
				const auto &definition = process_definition(state);
				if (definition.stop_effect)
					result.push_back(process_effect_invocation(address, state, *definition.stop_effect));
			}
	}
	return result;
}
// Expansion belongs to effect preparation even when the caller only quotes
// fees. Visit before expanding so a prepaid exit can skip its already-paid tree.
template <typename Visit>
void expand_process_effect_frames(PreparedEffectGroup &scope, Visit &&visit, const char *capacity_failure)
{
	if (scope.invocations.size() > rule_instance_limit)
		throw std::runtime_error("Effect invocation batch capacity exhausted");
	scope.invocation_roots.clear();
	for (std::size_t i = 0; i < scope.invocations.size(); ++i)
		scope.invocation_roots.push_back(i);
	for (std::size_t i = 0; i < scope.invocations.size(); ++i)
	{
		scope.failed_invocation = i;
		const auto frame = scope.invocations[i];
		if (!visit(i, frame))
			continue;
		for (auto &stop : process_removal_frames(scope, frame))
		{
			if (scope.invocations.size() >= rule_instance_limit)
				throw std::runtime_error(capacity_failure);
			const auto address = custom_property_address(stop.rule_context->owner);
			if (scope.prepaid_process_stops.contains({address, stop.source}))
			{
				stop.execution.energy = 0;
				stop.execution.costs.clear();
			}
			scope.invocations.push_back(std::move(stop));
			scope.invocation_roots.push_back(scope.invocation_roots.at(i));
		}
	}
}
PreparedEffectGroup prepare_effect_groups(std::span<const EffectGroupInvocation> invocations,
                                          PreparedEffectGroup quote = {})
{
	quote.invocations.assign(invocations.begin(), invocations.end());
	for (auto &frame : quote.invocations)
		if (!frame.hit_context && frame.rule_context && frame.rule_context->hit.target)
			frame.hit_context = frame.rule_context->hit;
	quote.tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	quote.epoch = effect_commit_epoch;
	quote.next_active = quote.original_next_active = next_rule_instance;
	try
	{
		std::vector<std::vector<PreparedEffectCost>> costs;
		costs.reserve(invocations.size());
		// Resolve immutable addresses and create all candidate networks before
		// staging any stock. Every alias then addresses the same transaction copy.
		expand_process_effect_frames(
		    quote,
		    [&](std::size_t, const EffectGroupInvocation &frame)
		    {
			    costs.push_back(resolve_effect_costs(quote, frame));
			    for (const auto &cost : costs.back())
				    if (cost.energy_store != no_resource && cost.amount)
					    quote.payments.try_emplace(cost.payer, entities.at(cost.payer).get<ShipResources>().network);
			    return true;
		    },
		    "Process stop effect batch capacity exhausted");
		prepare_thermal_networks(quote);
		for (std::size_t i = 0; i < quote.invocations.size(); ++i)
		{
			quote.failed_invocation = i;
			stage_effect_costs(quote, quote.invocations[i], costs[i]);
		}
		for (std::size_t i = 0; i < quote.invocations.size(); ++i)
		{
			quote.failed_invocation = i;
			stage_effect_invocation(quote, i);
		}
		quote.failed_invocation = static_cast<std::size_t>(-1);
	}
	catch (const std::exception &error)
	{
		quote.failure = error.what();
	}
	return quote;
}
PreparedEffectGroup prepare_effect_group(const EffectGroupDefinition &execution, EntityId protected_ship,
                                         EntityId attacker, const std::string &source,
                                         const PayloadHitContext *hit_context = nullptr,
                                         const std::string &original_equipment = {},
                                         const NativeRuleContext *rule_context = nullptr,
                                         std::int64_t output_permille = 1000, bool maintained = false)
{
	EffectGroupInvocation frame;
	frame.execution = execution;
	frame.protected_ship = protected_ship;
	frame.attacker = attacker;
	frame.source = source;
	frame.original_equipment = original_equipment;
	frame.output_permille = output_permille;
	frame.maintained = maintained;
	if (hit_context)
		frame.hit_context = *hit_context;
	if (rule_context)
		frame.rule_context = *rule_context;
	return prepare_effect_groups(std::span<const EffectGroupInvocation>(&frame, 1));
}
bool effect_group_quote_stale(const PreparedEffectGroup &quote) const
{
	if (quote.epoch != effect_commit_epoch || quote.tick != static_cast<std::int64_t>(ecs.get<Clock>().tick))
		return true;
	for (const auto &[id, payment] : quote.payments)
	{
		if (!entities.contains(id) || !entities.at(id).has<ShipResources>())
			return true;
		const auto &current = payment.destination ? *payment.destination : entities.at(id).get<ShipResources>().network;
		if (current.revision() != payment.revision || current.stores().size() != payment.stores.size() ||
		    current.coolant_sources().size() != payment.coolant.size())
			return true;
		for (std::size_t i = 0; i < payment.stores.size(); ++i)
			if (current.stores()[i].stock.revision() != payment.stores[i])
				return true;
		for (std::size_t i = 0; i < payment.coolant.size(); ++i)
			if (current.coolant_sources()[i].stock.revision() != payment.coolant[i])
				return true;
	}
	for (const auto &[id, original] : quote.original_shields)
		if (!entities.contains(id) || !entities.at(id).has<ShieldComponent>() ||
		    !same_effect_shields(entities.at(id).get<ShieldComponent>(), original))
			return true;
	for (const auto &[id, original] : quote.original_properties)
		if (!entities.contains(id) || !entities.at(id).has<PropertyEffectOwner>() ||
		    !same_effect_properties(read_property_effects(entities.at(id)), original))
			return true;
	for (const auto &[address, original] : quote.original_custom_properties)
	{
		const auto *current = custom_property_state(address);
		if (!current || *current != original)
			return true;
	}
	for (const auto &[id, original] : quote.original_abilities)
		if (!entities.contains(id) || !entities.at(id).has<AbilityRuntime>() ||
		    !same_effect_abilities(entities.at(id).get<AbilityRuntime>(), original))
			return true;
	for (const auto &[address, original] : quote.original_processes)
	{
		const auto *current = process_runtime(address);
		if (!current || *current != original)
			return true;
	}
	if (quote.original_active_effects &&
	    (next_rule_instance != quote.original_next_active ||
	     !same_effect_active_rules(active_rule_snapshot(), *quote.original_active_effects)))
		return true;
	for (const auto &[id, equipment] : quote.equipment)
		if (!effect_group_equipment_available(id, equipment))
			return true;
	for (const auto &damage : quote.damages)
		if (damage.outcome && !payload_context_available(damage.context))
			return true;
	try
	{
		for (const auto &target : quote.targets)
			effect_group_target(target.target, target.ordnance);
	}
	catch (const std::exception &)
	{
		return true;
	}
	return false;
}
PreparedEffectGroup reprepare_effect_group(const PreparedEffectGroup &previous)
{
	if (previous.consumed)
	{
		PreparedEffectGroup closed;
		closed.failure = "Effect quote was already consumed";
		closed.consumed = true;
		return closed;
	}
	auto quote = prepare_effect_groups(previous.invocations);
	quote.defer_damage = previous.defer_damage;
	return quote;
}
std::string commit_effect_group(PreparedEffectGroup &quote)
{
	if (!quote.failure.empty())
		return quote.failure;
	if (quote.consumed)
		return "Effect quote was already consumed";
	if (effect_group_quote_stale(quote))
		return "Effect quote became stale";
	if (effect_commit_epoch == std::numeric_limits<std::uint64_t>::max())
		return "Effect commit epoch exhausted";
	if (quote.effect_failures.size() > 9007199254740991ULL - next_rule_job)
		return "Effect result identity exhausted";
	if (const auto failure = rule_origin_failure(quote); !failure.empty())
		return failure;
	if (quote.defer_damage && !quote.damages.empty())
		if (const auto failure = prepared_damage_failure(quote); !failure.empty())
			return failure;
	if (!quote.stocks.commit())
		return "Store transaction became stale";
	quote.consumed = true;
	++effect_commit_epoch;
	for (const auto &failure : quote.effect_failures)
		record_effect_result(rule_results, next_rule_job++, "failed: " + failure);
	begin_custom_property_batch();

	for (auto &[id, payment] : quote.payments)
		if (payment.destination)
			*payment.destination = std::move(payment.network);
		else
			entities.at(id).get_mut<ShipResources>().network = std::move(payment.network);
	for (auto id : quote.changed_inventories)
		if (auto *inventory = entities.at(id).try_get_mut<MaterialInventory>())
			++inventory->revision;
	for (auto &[id, value] : quote.shields)
		entities.at(id).set<ShieldComponent>(std::move(value));
	for (auto &[id, value] : quote.properties)
		commit_property_effects(entities.at(id), value);
	for (auto &[address, value] : quote.custom_properties)
		commit_custom_property_state(address, value);
	for (auto &[address, value] : quote.processes)
		commit_process_runtime(address, value);
	if (quote.active_effects)
	{
		commit_active_rule_effects(*quote.active_effects);
		next_rule_instance = quote.next_active;
	}
	finish_custom_property_batch();
	for (auto &[id, value] : quote.abilities)
	{
		const auto &old = entities.at(id).get<AbilityRuntime>();
		for (const auto &[key, grant] : old.grants)
			if (!value.grants.contains(key) || value.grants.at(key).generation != grant.generation)
				clear_retired_ability_control(id, grant, value);
		entities.at(id).set<AbilityRuntime>(std::move(value));
	}
	if (!quote.abilities.empty())
		sync_psychic_contributions();
	remember_rule_origins(quote);
	if (quote.defer_damage)
	{
		if (!quote.damages.empty())
			queue_prepared_damage(quote);
		return {};
	}
	for (const auto &damage : quote.damages)
	{
		const auto &frame = quote.invocations.at(damage.invocation);
		const auto id = damage.target;
		const auto amount = damage.amount;
		auto ship = damage.outcome ? payload_target(damage.context) : entities.at(id);
		auto context = effect_invocation_context(frame, quote.tick);
		if (damage.outcome)
		{
			apply_payload_damage(ship, amount, damage.context, &context);
			continue;
		}
		auto heading = ship.get<Pose>().heading;
		const auto instigator = context.instigator.ordnance ? EntityId{0} : context.instigator.entity;
		if (const auto origin = entities.find(instigator); origin != entities.end())
			heading = combat_bearing(ship.get<Pose>().position - origin->second.get<Pose>().position);
		apply_payload_damage(ship, amount, PayloadHitContext{damage.target}, &context, heading);
	}
	return {};
}
std::string execute_effect_group(const EffectGroupDefinition &execution, EntityId protected_ship, EntityId attacker,
                                 const std::string &source, const PayloadHitContext *hit_context = nullptr,
                                 const std::string &original_equipment = {},
                                 const NativeRuleContext *rule_context = nullptr, std::int64_t output_permille = 1000,
                                 bool maintained = false)
{
	auto quote = prepare_effect_group(execution, protected_ship, attacker, source, hit_context, original_equipment,
	                                  rule_context, output_permille, maintained);
	return commit_effect_group(quote);
}

std::string execute_scenario_effect_group(const ScenarioEffectRequest &request)
{
	const auto found = effect_groups.find(request.definition);
	if (found == effect_groups.end())
		return "Unknown effect group";
	auto source = "scenario." + std::to_string(request.token.instance) + ".group." +
	              std::to_string(request.token.activation) + "." + std::to_string(request.token.task);
	for (std::size_t i = 0; i < request.token.call_path.size(); ++i)
		source += "." + std::to_string(request.token.call_path[i]) + ":" +
		          std::to_string(request.token.call_activations.at(i));
	return execute_effect_group(found->second, request.target, request.source ? request.source : request.target,
	                            source);
}

void validate_stock_claim_owners()
{
	for (const auto &[id, ship] : entities)
	{
		std::map<const StockStore *, std::set<std::string>> processor_claims;
		std::set<const StockStore *> magazines;
		if (const auto *equipment = ship.try_get<EquipmentIndex>())
			for (auto child : equipment->values)
				if (const auto *mechanism = child.try_get<CombatMechanism>();
				    mechanism && mechanism->definition.kind == OperatingSystem::Magazine)
					magazines.insert(&physical_stock(ship, mechanism->get<MagazineState>().stock));
		if (const auto *processors = ship.try_get<ShipProcessors>())
			for (const auto &[name, processor] : processors->values)
			{
				if (!processor.state().active)
					continue;
				for (const auto &recipe : processor.definition().recipes)
					if (recipe.id == processor.state().recipe)
						for (const auto &[resource, quantity] : recipe.outputs)
						{
							auto &stock = named_stock(ship, processor.definition().outputs.at(resource)).stock;
							processor_claims[&stock].insert(processor.output_claim(resource));
						}
			}
		const auto validate = [&](const StockStore &stock, bool physical)
		{
			for (const auto &[key, claim] : stock.stock_claims())
				if (!physical || !key.starts_with("cargo:"))
					throw std::runtime_error("Saved stock claim has no native owner: " + key);
			for (const auto &[key, claim] : stock.capacity_claims())
			{
				if (key.starts_with("processor:") && processor_claims[&stock].contains(key))
					continue;
				if (physical && (key.starts_with("batch:") || key.starts_with("cargo:")))
					continue;
				if (key.starts_with("weapon:") && magazines.contains(&stock))
					continue;
				throw std::runtime_error("Saved receiving commitment has no native owner: " + key);
			}
		};
		if (const auto *physical = ship.try_get<ShipStockStores>())
			for (const auto &[key, stock] : physical->stores)
				validate(stock, true);
		if (const auto *power = ship.try_get<ShipResources>())
		{
			for (const auto &state : power->network.stores())
				validate(state.stock, false);
			for (const auto &state : power->network.coolant_sources())
				validate(state.stock, false);
		}
	}
}
