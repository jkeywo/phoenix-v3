// Included inside World::Impl. Resolve an explicit store identity across native
// mechanisms; there is no implicit ship-wide pool or remote supply lookup.
struct NamedStock
{
	StockStore &stock;
	bool physical;
};
NamedStock named_stock(flecs::entity ship, const std::string &name, ResourceNetwork *prepared_network = nullptr) const
{
	StockStore *result = nullptr;
	bool physical_store = false;
	const auto accept = [&](StockStore &stock, const std::string &key, bool physical)
	{
		if (name != key && name != stock.definition().id)
			return;
		if (result && result != &stock)
			throw std::runtime_error("Ambiguous store identity: " + name);
		result = &stock;
		physical_store = physical;
	};
	if (auto *physical = ship.try_get_mut<ShipStockStores>())
		for (auto &[key, stock] : physical->stores)
			accept(stock, key, true);
	if (auto *physical = ship.try_get_mut<ShipStockStores>())
		if (const auto *inventory = ship.try_get<MaterialInventory>())
		{
			const auto &stores = definitions.at(ship.get<Definition>().name).logistics.stores;
			for (std::size_t i = 0; i < stores.size(); ++i)
				if (name == stores[i].id)
					accept(physical->stores.at(inventory->stores.at(i).stock), name, true);
		}
	if (auto *power = ship.try_get_mut<ShipResources>())
	{
		auto &network = prepared_network ? *prepared_network : power->network;
		for (ResourceIndex i = 0; i < network.stores().size(); ++i)
			accept(network.stock(i), "energy:" + network.definition().stores[i].id, false);
		for (ResourceIndex i = 0; i < network.coolant_sources().size(); ++i)
			accept(network.coolant_stock(i), "coolant:" + network.definition().coolant_sources[i].id, false);
	}
	if (!result)
		throw std::runtime_error("Unknown store: " + name);
	return {*result, physical_store};
}

std::string execute_effect_group(const ShieldReactionDefinition &execution, EntityId protected_ship, EntityId attacker,
                                 const std::string &source)
{
	std::map<EntityId, ShieldComponent> shields;
	std::map<EntityId, PropertyEffects> properties;
	std::set<EntityId> changed_inventories;
	// Only native entry funding needs network preparation: it also changes
	// discharge budgets and heat. Ordinary stock operations stage touched stores.
	std::optional<ResourceNetwork> payment;
	std::vector<std::pair<EntityId, std::int64_t>> damages;
	StockTransaction stocks;
	const auto owner = [&](EntityId id)
	{
		const auto at = entities.find(id);
		if (at == entities.end() || combat_dead(at->second))
			throw std::runtime_error("Required target unavailable");
		return at->second;
	};
	const auto stock = [&](EntityId id, const std::string &name) -> StockStore &
	{
		auto resolved = named_stock(owner(id), name, id == protected_ship && payment ? &*payment : nullptr);
		if (resolved.physical)
			changed_inventories.insert(id);
		return resolved.stock;
	};
	try
	{
		// Entry funding is reserved against pre-operation stock. The group cannot
		// manufacture its own entry payment using an earlier add/fill operation.
		if (execution.energy)
		{
			auto payer = owner(protected_ship);
			const auto fitted = instance(payer, execution.payer);
			if (!fitted || !fitted->entity.has<ResourceBinding>() || !fitted->entity.get<Equipment>().enabled ||
			    structurally_disabled(payer, execution.payer) || fault_effectiveness(payer, execution.payer) == 0)
				throw std::runtime_error("Payer equipment unavailable");
			const auto &power = payer.get<ShipResources>();
			payment = power.network;
			const auto index = power.definition.systems.at(fitted->entity.get<ResourceBinding>().slot).store;
			if (index >= power.network.stores().size() || !payment->spend_external(index, execution.energy))
				throw std::runtime_error("Energy payment unavailable");
		}
		for (const auto &op : execution.operations)
		{
			const auto id = op.target == "attacker" ? attacker : protected_ship;
			auto ship = owner(id);
			if (op.kind == ShieldReactionKind::Damage)
			{
				if (!ship.has<CombatStructure>())
					throw std::runtime_error("Damage target incompatible");
				damages.emplace_back(id, op.damage);
			}
			else if (op.kind >= ShieldReactionKind::StoreAdd)
			{
				auto &destination = stock(id, op.store);
				bool valid = false;
				switch (op.kind)
				{
				case ShieldReactionKind::StoreAdd:
					valid = stocks.add(destination, op.resource, op.amount);
					break;
				case ShieldReactionKind::StoreConsume:
					valid = stocks.consume(destination, op.resource, op.amount);
					break;
				case ShieldReactionKind::StoreSet:
					valid = stocks.set(destination, op.resource, op.amount);
					break;
				case ShieldReactionKind::StoreFill:
					valid = stocks.fill(destination, op.resource);
					break;
				case ShieldReactionKind::StoreTransfer:
					valid = stocks.transfer(stock(op.target == "attacker" ? protected_ship : attacker, op.from_store),
					                        destination, op.resource, op.amount);
					break;
				default:
					break;
				}
				if (!valid)
					throw std::runtime_error("Store operation cannot satisfy exact stock/capacity commitment");
			}
			else
			{
				const auto kind =
				    op.equipment.empty() ? EffectApplicationKind::Shield : EffectApplicationKind::Modifier;
				ShieldComponent *shield = nullptr;
				PropertyEffects *property = nullptr;
				if (kind == EffectApplicationKind::Shield)
				{
					if (!ship.has<ShieldComponent>())
						throw std::runtime_error("Incompatible shield target");
					shield = &shields.try_emplace(id, ship.get<ShieldComponent>()).first->second;
				}
				else
				{
					if (!ship.has<PropertyEffects>())
						throw std::runtime_error("Incompatible modifier target");
					property = &properties.try_emplace(id, ship.get<PropertyEffects>()).first->second;
				}
				prepare_effect_application(ship,
				                           {kind,
				                            op.kind == ShieldReactionKind::Remove ? EffectApplicationOperation::Remove
				                                                                  : EffectApplicationOperation::Apply,
				                            source + "." + op.grant, op.equipment, op.effect},
				                           shield, property, ecs.get<Clock>().tick);
			}
		}
		std::size_t reserve = 0;
		for (const auto &[id, amount] : damages)
		{
			const auto *shield = shields.contains(id) ? &shields.at(id) : owner(id).try_get<ShieldComponent>();
			if (shield)
				for (const auto &c : shield->contributions)
					reserve += c.definition.shield.reactions.size();
		}
		if (shield_reactions.size() + reserve > 8192 || next_shield_reaction + reserve >= 9007199254740991ULL ||
		    next_shield_hit + damages.size() >= 9007199254740991ULL)
			throw std::runtime_error("Effect reaction capacity exhausted before execution");
		if (!stocks.commit())
			throw std::runtime_error("Store transaction became stale");
	}
	catch (const std::exception &error)
	{
		return error.what();
	}
	if (payment)
		entities.at(protected_ship).get_mut<ShipResources>().network = std::move(*payment);
	for (auto id : changed_inventories)
		if (auto *inventory = entities.at(id).try_get_mut<MaterialInventory>())
			++inventory->revision;
	for (auto &[id, value] : shields)
		entities.at(id).set<ShieldComponent>(std::move(value));
	for (auto &[id, value] : properties)
		entities.at(id).set<PropertyEffects>(std::move(value));
	for (const auto &[id, amount] : damages)
	{
		auto ship = entities.at(id);
		auto heading = ship.get<Pose>().heading;
		if (const auto origin = entities.find(protected_ship); origin != entities.end())
			heading = combat_bearing(ship.get<Pose>().position - origin->second.get<Pose>().position);
		const auto remaining = absorb_shields(ship, heading, amount, 0, nullptr, protected_ship);
		if (remaining)
			combat_damage(ship, 0, remaining);
	}
	return {};
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
