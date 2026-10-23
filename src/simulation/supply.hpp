// Collection and barter extend cargo jobs; all goods travel through MaterialBatch.
std::optional<CargoDelivery> supply_manifest(std::size_t index, std::int64_t requested = -1) const
{
	const auto &d = away_definitions.at(index);
	const auto &m = away_missions.at(index);
	if (!d.supply)
		return {};
	const auto ship = away_ship(d), craft = away_transport_entity(ship, m.transport);
	if (!craft || craft == ship || !entities.at(craft).has<MaterialInventory>() ||
	    !entities.at(d.target).has<MaterialInventory>() || !entities.at(ship).has<MaterialInventory>())
		return {};
	const auto &offer = *d.supply;
	auto store = [&](EntityId id, const std::string &name) -> std::optional<std::size_t>
	{
		const auto &rows = definition(id).logistics.stores;
		for (std::size_t i = 0; i < rows.size(); ++i)
			if (rows[i].id == name)
				return i;
		return {};
	};
	const auto home = store(ship, offer.home_store), provider = store(d.target, offer.provider_store);
	if (!home || !provider || !material_accepts(ship, *home, offer.material) ||
	    !material_accepts(d.target, *provider, offer.material))
		return {};
	const auto &targets = entities.at(ship).get<MaterialInventory>().stores.at(*home).targets;
	auto committed = material_quantity(ship, *home, offer.material);
	for (const auto &batch : material_batches)
		if (batch.destination == ship && batch.to == *home && batch.material == offer.material)
			committed += batch.quantity;
	for (const auto &job : cargo_deliveries)
		if (job.material == offer.material &&
		    (job.collection ? job.source == ship && job.from == *home : job.target == ship && job.to == *home))
			committed += job.reserved;
	const auto deficit =
	    targets.contains(offer.material) ? std::max<std::int64_t>(0, targets.at(offer.material) - committed) : 0;
	for (std::size_t cargo = 0; cargo < definition(craft).logistics.stores.size(); ++cargo)
	{
		if (!material_accepts(craft, cargo, offer.material))
			continue;
		bool empty = true;
		for (const auto &type : material_store(craft, cargo).accepts)
			empty &= material_quantity(craft, cargo, type) == 0;
		if (!empty)
			continue;
		const auto volume = definition(ship).logistics.materials.at(offer.material).volume;
		if (definition(craft).logistics.materials.at(offer.material).volume != volume ||
		    definition(d.target).logistics.materials.at(offer.material).volume != volume)
			continue;
		const auto owner = enabled_system_owner(ship, away_dispatch_system(ship, d.dispatch));
		const bool manual_quantity = station_stars(owner) >= 3 && console_operator(owner) != 0;
		const auto chosen = requested >= 0 ? requested : manual_quantity ? m.supply_quantity : 0;
		auto batches =
		    std::min({offer.limit, chosen ? chosen : deficit, material_available(d.target, *provider, offer.material),
		              material_space(ship, *home, offer.material), material_space(craft, cargo, offer.material)}) /
		    offer.give;
		if (offer.pay)
		{
			if (!material_accepts(ship, *home, offer.payment) || !material_accepts(craft, cargo, offer.payment) ||
			    !material_accepts(d.target, *provider, offer.payment))
				continue;
			const auto payment_volume = definition(ship).logistics.materials.at(offer.payment).volume;
			if (definition(craft).logistics.materials.at(offer.payment).volume != payment_volume ||
			    definition(d.target).logistics.materials.at(offer.payment).volume != payment_volume)
				continue;
			batches = std::min({batches, material_available(ship, *home, offer.payment) / offer.pay,
			                    material_space(craft, cargo, offer.payment) / offer.pay,
			                    material_space(d.target, *provider, offer.payment) / offer.pay});
		}
		if (batches <= 0)
			continue;
		bool port = false;
		for (const auto &a : definition(craft).docking)
			for (const auto &b : definition(d.target).docking)
				port |= a.compatibility == b.compatibility;
		if (!port || !dock_known(enabled_system_owner(ship, entities.at(craft).get<CraftState>().bay), d.target))
			continue;
		CargoDelivery job;
		job.collection = true;
		job.mission = d.id;
		job.source = ship;
		job.carrier = craft;
		job.target = d.target;
		job.from = *home;
		job.to = *provider;
		job.cargo = cargo;
		job.material = offer.material;
		job.payment = offer.payment;
		job.amount = batches * offer.give;
		job.reserved = job.amount;
		job.payment_amount = batches * offer.pay;
		return job;
	}
	return {};
}
bool supply_pending(const CargoDelivery &job) const
{
	return std::any_of(material_requests.begin(), material_requests.end(),
	                   [&](const auto &r)
	                   {
		                   return r.cargo == job.id;
	                   }) ||
	       std::any_of(material_batches.begin(), material_batches.end(),
	                   [&](const auto &b)
	                   {
		                   return b.cargo == job.id;
	                   });
}
bool supply_transfer(CargoDelivery &job, EntityId source, std::size_t from, EntityId destination, std::size_t to,
                     const std::string &material, std::int64_t quantity)
{
	if (quantity <= 0)
		return true;
	if (material_requests.size() >= 4096 || next_material_request >= 9007199254740991ULL)
		return false;
	request_material(source, from, destination, to, material, quantity, false);
	material_requests.back().cargo = job.id;
	return true;
}
void supply_return(CargoDelivery &job)
{
	job.phase = "returning";
	const auto craft = entities.at(job.carrier);
	if (craft.has<DockConnection>())
	{
		auto &dock = craft.get_mut<DockConnection>();
		if (dock.phase == "approach")
			detach_dock(job.carrier, false);
		else
		{
			dock.phase = "separating";
			dock.progress = 0;
		}
	}
	craft_return(craft);
	for (auto &mission : away_missions)
		if (mission.supply_job == job.id)
			return_away_escorts(mission);
}
void step_supply_cargo(CargoDelivery &job)
{
	const auto e = entities.at(job.carrier);
	if (combat_dead(e) || e.get<CraftState>().phase == CraftPhase::Lost)
	{
		job.phase = "lost";
		job.reserved = 0;
		std::erase_if(material_requests,
		              [&](const auto &r)
		              {
			              return r.cargo == job.id;
		              });
	}
	if (craft_link(job.carrier))
	{
		job.report = job.phase;
		job.report_tick = ecs.get<Clock>().tick;
	}
	if (job.phase == "complete" || job.phase == "lost")
		return;
	if (job.phase == "cancelling")
	{
		if (!supply_pending(job))
			supply_return(job);
		return;
	}
	if (job.phase != "returning" && job.phase != "home-unloading" &&
	    (e.get<CraftState>().order == "return" || ftl_preparing(job.source)))
	{
		request_cargo_return(job);
		return;
	}
	if (job.phase == "loading")
	{
		if (job.payment_amount && material_quantity(job.carrier, job.cargo, job.payment) < job.payment_amount)
			return;
		if (supply_pending(job) || !craft_link(job.carrier) || !bay_working(job.source, e.get<CraftState>().bay))
			return;
		for (const auto &mission : away_missions)
			if (mission.supply_job == job.id)
				for (const auto &[slot, escort] : mission.escorts)
					if (entities.at(escort).get<CraftState>().phase != CraftPhase::Flying)
						return;
		job.phase = "outbound";
		auto &state = e.get_mut<CraftState>();
		state.order = "transport";
		state.target = job.target;
		state.target_position = entities.at(job.target).get<Pose>().position;
		state.target_velocity = entities.at(job.target).get<Motion>().velocity;
		state.target_altitude = altitude(job.target);
		launch_craft(e);
	}
	if (job.phase == "outbound")
	{
		if (e.get<CraftState>().phase != CraftPhase::Flying)
			return;
		if (!e.has<DockConnection>())
			for (const auto &port : definition(job.carrier).docking)
				for (const auto &remote : definition(job.target).docking)
					if (port.compatibility == remote.compatibility && !e.has<DockConnection>())
						for (const auto &ctrl : e.get<EntityControls>().controllers)
							if (ctrl.definition.id == port.controller)
							{
								OperatingCommand c{job.carrier, "dock.assist",
								                   static_cast<std::int64_t>(e.get<DockingState>().revision), port.id};
								c.controller = ctrl.definition.id;
								c.generation = ctrl.generation;
								c.text = std::to_string(job.target) + "/" + remote.id;
								docking_command({job.carrier, ctrl.definition.console}, c, true, true);
							}
		if (!dock_ready(job.carrier, job.target))
			return;
		if (material_available(job.target, job.to, job.material, job.id) < job.amount)
		{
			request_cargo_return(job);
			return;
		}
		if (supply_transfer(job, job.carrier, job.cargo, job.target, job.to, job.payment, job.payment_amount))
			job.phase = "paying";
	}
	if (job.phase == "paying")
	{
		if (supply_pending(job) || job.paid != job.payment_amount)
			return;
		if (supply_transfer(job, job.target, job.to, job.carrier, job.cargo, job.material, job.amount))
			job.phase = "collecting";
	}
	if (job.phase == "collecting" && !supply_pending(job))
		supply_return(job);
	if (job.phase == "returning" && e.get<CraftState>().phase == CraftPhase::Docked && !supply_pending(job))
	{
		if (material_requests.size() > 4094 || next_material_request >= 9007199254740990ULL)
			return;
		job.phase = "home-unloading";
		supply_transfer(job, job.carrier, job.cargo, job.source, job.from, job.material,
		                material_quantity(job.carrier, job.cargo, job.material));
		if (!job.payment.empty())
			supply_transfer(job, job.carrier, job.cargo, job.source, job.from, job.payment,
			                material_quantity(job.carrier, job.cargo, job.payment));
	}
	if (job.phase == "home-unloading" && !supply_pending(job))
	{
		job.phase = "complete";
		job.reserved = 0;
	}
}
void advance_supply_mission(std::size_t index)
{
	auto &m = away_missions.at(index);
	if (!m.supply_job)
		return;
	for (const auto &job : cargo_deliveries)
		if (job.id == m.supply_job)
		{
			if (job.phase == "lost")
			{
				if (m.phase != AwayPhase::Lost)
				{
					m.phase = AwayPhase::Lost;
					m.outcome = "fail:Transport lost";
					++m.revision;
					return_away_escorts(m);
				}
			}
			else if (job.phase == "complete" && m.phase != AwayPhase::Recovered)
			{
				finish_away_recovery(away_ship(away_definitions.at(index)), m);
				m.outcome = job.delivered == job.amount && job.paid == job.payment_amount && !job.cancelled
				                ? "complete:Supplies delivered"
				                : "fail:Supply exchange incomplete";
			}
			else if ((job.phase == "returning" || job.phase == "home-unloading") && m.phase != AwayPhase::Inbound)
			{
				m.phase = AwayPhase::Inbound;
				++m.revision;
			}
		}
}
