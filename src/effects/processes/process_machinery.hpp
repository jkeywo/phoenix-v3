// Included inside World::Impl. Native adapters keep physical state and commands;
// these lookups resolve the one authored execution permitted to drive a mechanism.
static std::string_view native_process_kind_name(NativeProcessKind kind)
{
	switch (kind)
	{
	case NativeProcessKind::Generation:
		return "generation";
	case NativeProcessKind::Propulsion:
		return "propulsion";
	case NativeProcessKind::Cooling:
		return "cooling";
	case NativeProcessKind::Processor:
		return "processor";
	case NativeProcessKind::Repair:
		return "repair";
	case NativeProcessKind::Medical:
		return "medical";
	case NativeProcessKind::Recovery:
		return "recovery";
	case NativeProcessKind::ServicingEnergy:
		return "servicing_energy";
	case NativeProcessKind::ServicingAmmunition:
		return "servicing_ammunition";
	case NativeProcessKind::Ftl:
		return "ftl";
	default:
		return "effects";
	}
}
std::string process_native_identity(const CustomPropertyAddress &address, const ProcessState &state,
                                    const ProcessDefinition &definition) const
{
	if (definition.native == NativeProcessKind::Effects)
		return {};
	const auto native = state.native_id.empty() ? address.equipment : state.native_id;
	return std::to_string(address.entity) + ":" + std::string(native_process_kind_name(definition.native)) + ":" +
	       native;
}
std::string process_native_blocker(const CustomPropertyAddress &address, const ProcessState &state,
                                   const ProcessDefinition &definition) const
{
	if (definition.native == NativeProcessKind::Effects)
		return {};
	const auto found = entities.find(address.entity);
	if (found == entities.end() || combat_dead(found->second) || departed(address.entity))
		return "Process source unavailable";
	const auto ship = found->second;
	// Dormant craft and their equipment are disabled in the native ECS queries.
	// Keep authored runs paused without charging upkeep for unavailable machinery.
	if (craft_dormant(ship))
		return "Native craft dormant";
	if (definition.native == NativeProcessKind::Recovery)
	{
		for (const auto &facility : this->definition(address.entity).personnel.facilities)
			if (facility.id == state.native_id)
				return {};
		return "Native recovery facility unavailable";
	}
	const auto equipment = address.equipment;
	const auto fitted = instance(ship, equipment);
	if (!fitted || !fitted->enabled || structurally_disabled(ship, equipment))
		return "Native machinery unavailable";
	if (definition.native == NativeProcessKind::Ftl)
	{
		const auto *ftl = fitted->entity.try_get<FtlReserve>();
		const auto *resources = ship.try_get<ShipResources>();
		if (!ftl || !resources || !fault_effectiveness(ship, equipment))
			return "Native FTL unavailable";
		if (resources->network.stores().at(ftl->definition.store).thermal_stop)
			return "Native machinery protected";
		return {};
	}
	if (definition.native == NativeProcessKind::Repair || definition.native == NativeProcessKind::Medical ||
	    definition.native == NativeProcessKind::ServicingEnergy ||
	    definition.native == NativeProcessKind::ServicingAmmunition)
		return {};
	const auto reserve_ready = [&](ResourceIndex index)
	{
		const auto *resources = ship.try_get<ShipResources>();
		if (!resources || index == no_resource)
			return true;
		return resources->network.operation_ready(index, true);
	};
	if ((definition.native == NativeProcessKind::Propulsion || definition.native == NativeProcessKind::Processor) &&
	    ship.has<ShipResources>() && fitted->entity.has<ResourceBinding>() &&
	    !reserve_ready(
	        ship.get<ShipResources>().definition.systems.at(fitted->entity.get<ResourceBinding>().slot).store))
		return "Native machinery protected";
	if (definition.native == NativeProcessKind::Propulsion)
	{
		if (const auto *flight = fitted->entity.try_get<FlightRuntime>())
			return flight->state.enabled && !dock_attached(address.entity) ? std::string{}
			                                                               : "Native machinery disabled";
		if (const auto *engine = fitted->entity.try_get<Engine>())
			return engine->enabled && !dock_attached(address.entity) ? std::string{} : "Native machinery disabled";
		if (fitted->entity.has<Steering>())
			return !dock_attached(address.entity) ? std::string{} : "Native machinery disabled";
		return "Native propulsion unavailable";
	}
	if (definition.native == NativeProcessKind::Processor)
	{
		const auto *processors = ship.try_get<ShipProcessors>();
		if (!processors || !processors->values.contains(state.native_id) ||
		    processors->values.at(state.native_id).definition().equipment != equipment)
			return "Native processor unavailable";
		return {};
	}
	const auto *resources = ship.try_get<ShipResources>();
	if (!resources || !fitted->entity.has<ResourceBinding>())
		return "Native resource machinery unavailable";
	const auto &binding = resources->definition.systems.at(fitted->entity.get<ResourceBinding>().slot);
	if (definition.native == NativeProcessKind::Generation)
	{
		if (binding.generator == no_resource ||
		    resources->network.definition().generators.at(binding.generator).id != state.native_id)
			return "Native generator unavailable";
		if (!resources->network.generators().at(binding.generator).enabled)
			return "Native machinery disabled";
		if (!reserve_ready(resources->network.definition().generators.at(binding.generator).store))
			return "Native machinery protected";
		return {};
	}
	if (binding.cooling == no_resource ||
	    resources->network.definition().cooling_links.at(binding.cooling).id != state.native_id)
		return "Native cooling unavailable";
	if (resources->network.cooling_links().at(binding.cooling).isolated)
		return "Native machinery disabled";
	const auto &link = resources->network.definition().cooling_links.at(binding.cooling);
	const auto &source = resources->network.definition().coolant_sources.at(link.source);
	if (!reserve_ready(source.power_store))
		return "Native machinery protected";
	if (resources->network.stores().at(link.store).heat > 0 &&
	    !resources->network.coolant_sources().at(link.source).available())
		return "Native coolant unavailable";
	return {};
}
struct NativeProcessView
{
	CustomPropertyAddress owner;
	const ProcessState *state = nullptr;
};
NativeProcessView native_process_view(EntityId entity, std::string_view kind, const std::string &native) const
{
	NativeProcessView result;
	for (const auto &address : custom_property_addresses(entity))
		if (const auto *runtime = process_runtime(address))
			for (const auto &[id, state] : runtime->processes)
			{
				const auto &definition = process_definition(state);
				const auto native_id = state.native_id.empty() ? address.equipment : state.native_id;
				if (native_process_kind_name(definition.native) != kind || native_id != native || !state.admitted ||
				    !state.requested || !state.blocker.empty())
					continue;
				if (result.state)
					throw std::runtime_error("Duplicate native process driver");
				result = {address, &state};
			}
	return result;
}
std::int64_t native_process_rate(EntityId entity, std::string_view kind, const std::string &native) const
{
	if (const auto *quote = native_machinery_quote(entity, kind, native))
		return quote->limit ? quote->frozen.rate_permille : 0;
	const auto view = native_process_view(entity, kind, native);
	return view.state ? view.state->rate_permille : 0;
}
struct ProcessMachineryQuote
{
	CustomPropertyAddress owner;
	ProcessState frozen;
	NativeProcessKind kind = NativeProcessKind::Effects;
	std::string native;
	std::int64_t full_work = 0, quoted_work = -1, work = 0, limit = resource_limit, activity = 0;
	std::int64_t power_rate = 0, applied_power_rate = 0;
	std::int64_t basic_available = 0, basic_full = 0;
	std::optional<Engine> basic_engine;
	std::optional<Steering> basic_steering;
	bool actual_recorded = false;
	bool discrete_request = false;
};
std::vector<ProcessMachineryQuote> process_machinery_quotes;
std::set<std::string> process_native_rejected_inputs;
ProcessMachineryQuote *native_machinery_quote(EntityId entity, std::string_view kind, const std::string &native)
{
	for (auto &quote : process_machinery_quotes)
		if (quote.owner.entity == entity && native_process_kind_name(quote.kind) == kind && quote.native == native)
			return &quote;
	return nullptr;
}
const ProcessMachineryQuote *native_machinery_quote(EntityId entity, std::string_view kind,
                                                    const std::string &native) const
{
	for (const auto &quote : process_machinery_quotes)
		if (quote.owner.entity == entity && native_process_kind_name(quote.kind) == kind && quote.native == native)
			return &quote;
	return nullptr;
}
std::int64_t native_process_price(const ProcessMachineryQuote &quote) const
{
	if (quote.limit == resource_limit)
		return 1000;
	return quote.full_work ? std::min<std::int64_t>(1000, quote.limit * 1000 / quote.full_work) : 0;
}
std::int64_t native_process_paid(EntityId entity, std::string_view kind, const std::string &native,
                                 std::int64_t paid) const
{
	const auto *quote = native_machinery_quote(entity, kind, native);
	return quote ? paid * native_process_price(*quote) / 1000 : 0;
}
std::int64_t native_process_work_limit(EntityId entity, std::string_view kind, const std::string &native) const
{
	const auto *quote = native_machinery_quote(entity, kind, native);
	return quote ? quote->limit : 0;
}
void begin_process_machinery_quotes()
{
	process_machinery_quotes.clear();
	process_native_rejected_inputs.clear();
	for (const auto &[entity, ship] : entities)
		for (const auto &address : custom_property_addresses(entity))
			if (const auto *runtime = process_runtime(address))
				for (const auto &[id, state] : runtime->processes)
					if (state.admitted && state.requested && state.blocker.empty())
					{
						const auto &definition = process_definition(state);
						if (definition.native == NativeProcessKind::Repair ||
						    definition.native == NativeProcessKind::Medical ||
						    definition.native == NativeProcessKind::Recovery ||
						    definition.native == NativeProcessKind::ServicingEnergy ||
						    definition.native == NativeProcessKind::ServicingAmmunition)
							continue;
						const auto native = state.native_id.empty() ? address.equipment : state.native_id;
						process_machinery_quotes.push_back({address, state, definition.native, native});
					}
}
void prepare_process_machinery_controls()
{
	for (const auto &[id, ship] : entities)
		if (auto *power = ship.try_get_mut<ShipResources>())
		{
			power->network.clear_machinery_work();
			for (ResourceIndex i = 0; i < power->network.generators().size(); ++i)
			{
				ResourceMachineryControl control{1000, 0, false};
				const auto &native = power->network.definition().generators[i].id;
				if (auto *quote = native_machinery_quote(id, "generation", native))
				{
					const auto &equipment = quote->owner.equipment;
					const auto scaled = effective_property(ship, equipment, "generation", quote->frozen.rate_permille);
					control = {std::min<std::int64_t>(1000000, scaled), quote->limit, true};
				}
				power->network.set_generator_work(i, control);
			}
			for (ResourceIndex i = 0; i < power->network.cooling_links().size(); ++i)
			{
				ResourceMachineryControl control{1000, 0, false};
				const auto &native = power->network.definition().cooling_links[i].id;
				if (auto *quote = native_machinery_quote(id, "cooling", native))
				{
					const auto scaled =
					    effective_property(ship, quote->owner.equipment, "cooling", quote->frozen.rate_permille);
					control = {std::min<std::int64_t>(1000000, scaled), quote->limit, true};
				}
				power->network.set_cooling_work(i, control);
			}
		}
}
void record_native_process_work(EntityId entity, std::string_view kind, const std::string &native,
                                std::int64_t available, std::int64_t completed)
{
	if (auto *quote = native_machinery_quote(entity, kind, native))
	{
		quote->work = quote->actual_recorded ? std::max(quote->work, completed) : completed;
		quote->actual_recorded = true;
		if (!quote->full_work)
			quote->full_work = available;
		quote->activity = quote->full_work ? std::min<std::int64_t>(1000, quote->work * 1000 / quote->full_work) : 0;
	}
}

std::int64_t quote_native_flight_work(flecs::entity child, const SystemResourceGrant *grant, bool full = false) const
{
	const auto &flight = child.get<FlightRuntime>();
	const auto id = child.parent().get<StableId>().id;
	const auto &equipment = child.get<Equipment>().id;
	if (!native_process_rate(id, "propulsion", equipment))
		return 0;
	if (!flight.state.enabled || craft_dormant(child.parent()) || structurally_disabled(child.parent(), equipment) ||
	    dock_attached(id))
		return 0;
	auto paid = full ? 1000 : (grant ? grant->supply_permille : 1000);
	if (!full)
		paid = static_cast<int>(native_process_paid(id, "propulsion", equipment, paid));
	const auto output = grant ? grant->overdrive_permille : 1000;
	const auto native_work = quote_flight_work(flight.definition, flight.state, flight.demand, paid, output);
	auto work = native_work.effort_milliticks + native_work.charging_milliticks;
	const auto charge = quote_flight_charge(flight.definition, flight.state);
	if (charge.progress && (full || !charge.energy || (grant && grant->discrete_paid)) && (!grant || !grant->blocked) &&
	    (full || native_process_work_limit(id, "propulsion", equipment) >= charge.progress))
		work += charge.progress;
	return work;
}
bool capture_process_native_work(const std::map<EntityId, std::optional<ShipResourceAllocation>> &allocations,
                                 bool stage)
{
	quote_basic_process_work();
	bool changed = false;
	for (auto &quote : process_machinery_quotes)
	{
		const auto ship = entities.at(quote.owner.entity);
		const auto &definition = process_definition(quote.frozen);
		const auto allocation = allocations.find(quote.owner.entity);
		const auto *native = allocation != allocations.end() && allocation->second ? &*allocation->second : nullptr;
		std::int64_t available = 0, full = 0;
		if (quote.kind == NativeProcessKind::Effects)
		{
			available = full = quote.frozen.rate_permille;
			if (definition.required_work)
				available = full =
				    std::min(available, std::max<std::int64_t>(0, definition.required_work - quote.frozen.progress));
		}
		else if (quote.kind == NativeProcessKind::Ftl && native)
		{
			const auto fitted = instance(ship, quote.owner.equipment);
			if (fitted && fitted->entity.has<FtlReserve>())
			{
				const auto &ftl = fitted->entity.get<FtlReserve>();
				available = native->prepared_network().telemetry().at(ftl.definition.store).accepted_incoming;
				full = std::max<std::int64_t>(available, ftl.definition.charge_rate / rate);
			}
		}
		else if (quote.kind == NativeProcessKind::Generation && native)
		{
			const auto index = native->prepared_network().definition().generator_ids.at(quote.native);
			const auto &receipt = native->prepared_network().generation_work().at(index);
			available = receipt.completed;
			full = receipt.available;
		}
		else if (quote.kind == NativeProcessKind::Cooling && native)
		{
			const auto index = native->prepared_network().definition().cooling_link_ids.at(quote.native);
			const auto &receipt = native->prepared_network().cooling_work().at(index);
			available = receipt.completed;
			full = receipt.available;
		}
		else if (quote.kind == NativeProcessKind::Processor)
		{
			const auto *processors = ship.try_get<ShipProcessors>();
			if (processors && processors->values.contains(quote.native))
			{
				const auto &processor = processors->values.at(quote.native);
				full = processor.state().prepared_work;
				const auto equipment = instance(ship, processor.definition().equipment);
				const auto paid =
				    native && equipment && equipment->entity.has<ResourceBinding>()
				        ? native->grants().at(equipment->entity.get<ResourceBinding>().slot).supply_permille
				        : 1000;
				available = processor.quoted_funded_work(
				    native_process_paid(quote.owner.entity, "processor", quote.native, paid));
			}
		}
		else if (quote.kind == NativeProcessKind::Propulsion)
		{
			const auto fitted = instance(ship, quote.owner.equipment);
			if (fitted && fitted->entity.has<FlightRuntime>())
			{
				const auto *grant = native && fitted->entity.has<ResourceBinding>()
				                        ? &native->grants().at(fitted->entity.get<ResourceBinding>().slot)
				                        : nullptr;
				available = quote_native_flight_work(fitted->entity, grant);
				full = quote_native_flight_work(fitted->entity, grant, true);
			}
			else if (fitted)
			{
				full = quote.basic_full;
				available = quote.basic_available;
			}
		}
		if (!quote.full_work)
			quote.full_work = full;
		available = std::min(available, quote.limit);
		quote.work = available;
		quote.activity = quote.full_work ? std::min<std::int64_t>(1000, available * 1000 / quote.full_work) : 0;
		if (stage && quote.quoted_work != available)
		{
			quote.quoted_work = available;
			stage_process_work(quote.owner, quote.frozen, definition, available, quote.activity);
			changed = true;
		}
	}
	return changed;
}
void reset_surviving_process_native_caps()
{
	for (auto &quote : process_machinery_quotes)
	{
		const auto permanent = [&](bool work)
		{
			const auto found = rule_upkeep_plans.find(process_funding_key(quote.owner, quote.frozen, work));
			return found != rule_upkeep_plans.end() && found->second.rejected && !found->second.provisional_shortage &&
			       !found->second.dependency_suspended;
		};
		quote.limit = permanent(false) || permanent(true) ? 0 : resource_limit;
		quote.quoted_work = -1;
	}
}
bool cap_process_native_work(bool producers_only = false)
{
	bool admission_changed = false;
	for (const auto &[key, plan] : rule_upkeep_plans)
		if (plan.rejected && !plan.provisional_shortage && !plan.dependency_suspended &&
		    process_native_rejected_inputs.insert(key).second)
			admission_changed = true;
	if (admission_changed)
	{
		// Only a genuine withdrawal changes the admitted reservation set. A raw
		// shortage remains provisional; do not freeze its loser at an old zero cap.
		reset_surviving_process_native_caps();
		return true;
	}
	bool changed = false;
	for (auto &quote : process_machinery_quotes)
	{
		if (producers_only && quote.kind != NativeProcessKind::Generation)
			continue;
		const auto idle = process_funding_output(quote.owner, quote.frozen.id, false);
		const auto work = process_funding_output(quote.owner, quote.frozen.id, true);
		auto maximum = quote.limit;
		if (idle < 1000)
			maximum = std::min(maximum, quote.full_work * idle / 1000);
		if (work < 1000)
			maximum = std::min(maximum, std::max<std::int64_t>(0, quote.quoted_work) * work / 1000);
		if (maximum != quote.limit)
		{
			quote.limit = maximum;
			changed = true;
		}
	}
	return changed;
}

void capture_process_native_requests()
{
	for (auto &quote : process_machinery_quotes)
	{
		const auto ship = entities.at(quote.owner.entity);
		const auto fitted = instance(ship, quote.owner.equipment);
		if (!fitted || !ship.has<ShipResources>() || !fitted->entity.has<ResourceBinding>())
			continue;
		if (quote.kind == NativeProcessKind::Propulsion && fitted->entity.has<FlightRuntime>())
		{
			quote.power_rate = fitted->entity.get<FlightRuntime>().demand.power_rate;
			quote.discrete_request =
			    ship.get<ShipResources>().requests.at(fitted->entity.get<ResourceBinding>().slot).discrete;
		}
		else if (quote.kind == NativeProcessKind::Ftl)
			quote.power_rate =
			    ship.get<ShipResources>().requests.at(fitted->entity.get<ResourceBinding>().slot).extra_rate;
		else if (quote.kind == NativeProcessKind::Processor)
			if (const auto *processors = ship.try_get<ShipProcessors>();
			    processors && processors->energy_rates.contains(quote.native))
				quote.power_rate = processors->energy_rates.at(quote.native);
		quote.applied_power_rate = quote.power_rate;
	}
}
void price_process_native_requests()
{
	for (auto &quote : process_machinery_quotes)
	{
		const auto ship = entities.at(quote.owner.entity);
		const auto fitted = instance(ship, quote.owner.equipment);
		auto *power = ship.try_get_mut<ShipResources>();
		if (!fitted || !power || !fitted->entity.has<ResourceBinding>())
			continue;
		auto &request = power->requests.at(fitted->entity.get<ResourceBinding>().slot);
		const auto fraction = native_process_price(quote);
		const auto desired = (quote.power_rate * fraction + 999) / 1000;
		if (request.extra_rate < quote.applied_power_rate)
			throw std::runtime_error("Native process lost its operating request");
		request.extra_rate = request.extra_rate - quote.applied_power_rate + desired;
		quote.applied_power_rate = desired;
		if (quote.kind == NativeProcessKind::Propulsion && quote.discrete_request)
		{
			request.discrete = quote.limit >= 1000;
			if (!request.discrete)
				request.discrete_amount = 0;
		}
		if (quote.kind == NativeProcessKind::Cooling && !fraction)
			request.duty_permille = 0;
		if (quote.kind == NativeProcessKind::Ftl)
		{
			const auto &ftl = fitted->entity.get<FtlReserve>().definition;
			const auto base = ftl.charge_rate * quote.frozen.rate_permille / 1000;
			const auto maximum = quote.limit >= base / rate + 1 ? base : std::min(base, quote.limit * rate);
			power->network.set_store_enabled(ftl.store, maximum > 0);
			power->network.set_storage_limits(ftl.store, maximum,
			                                  power->definition.network.stores.at(ftl.store).discharge_rate);
		}
	}
}
