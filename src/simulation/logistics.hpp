// Typed material simulation. Strings/JSON are confined to commands and projections.
std::vector<std::pair<EntityId, flecs::entity>> inventory_rows() const
{
	std::vector<std::pair<EntityId, flecs::entity>> rows;
	inventory_query.each(
	    [&](flecs::entity e, const StableId &id, MaterialInventory &)
	    {
		    rows.emplace_back(id.id, e);
	    });
	std::sort(rows.begin(), rows.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.first < b.first;
	          });
	return rows;
}
const MaterialStoreDefinition &material_store(EntityId id, std::size_t store) const
{
	return definition(id).logistics.stores.at(store);
}
bool material_accepts(EntityId id, std::size_t store, const std::string &m) const
{
	const auto &s = material_store(id, store);
	return std::find(s.accepts.begin(), s.accepts.end(), m) != s.accepts.end();
}
StockStore &physical_stock(flecs::entity ship, const std::string &key) const
{
	return ship.get_mut<ShipStockStores>().stores.at(key);
}
bool select_physical_resource(flecs::entity ship, const std::string &key, const std::string &resource, bool execute)
{
	auto &stock = physical_stock(ship, key);
	auto candidate = stock;
	if (!candidate.select(resource))
		return false;
	if (!execute || stock.selected() == resource)
		return true;
	if (!stock.select(resource))
		return false;
	if (auto *inventory = ship.try_get_mut<MaterialInventory>())
	{
		const auto id = ship.get<StableId>().id;
		for (std::size_t i = 0; i < inventory->stores.size(); ++i)
			if (inventory->stores[i].stock == key)
			{
				std::erase_if(inventory->stores[i].targets,
				              [&](const auto &target)
				              {
					              return target.first != resource;
				              });
				std::erase_if(material_requests,
				              [&](const auto &request)
				              {
					              return request.material != resource &&
					                     ((request.destination == id && request.to == i) ||
					                      (request.source == id && request.from == i));
				              });
			}
		++inventory->revision;
	}
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *weapon = child.try_get_mut<CombatMechanism>();
		    weapon && weapon->definition.kind == OperatingSystem::Weapon)
		{
			auto &state = weapon->get<WeaponState>();
			const auto &magazines = weapon->definition.get<WeaponDefinition>().magazines;
			if (!magazines.empty() && instance(ship, magazines.at(state.selected_magazine))
			                                  ->entity.get<CombatMechanism>()
			                                  .get<MagazineState>()
			                                  .stock == key)
				state.loading = 0;
		}
	return true;
}
StockStore &material_stock(EntityId id, std::size_t store) const
{
	return physical_stock(entities.at(id), entities.at(id).get<MaterialInventory>().stores.at(store).stock);
}
std::int64_t material_size(EntityId id, std::size_t store, const std::string &m) const
{
	return material_stock(id, store).size(m);
}
std::string material_batch_claim(std::uint64_t id) const
{
	return "batch:" + std::to_string(id);
}
std::int64_t material_quantity(EntityId id, std::size_t store, const std::string &m) const
{
	return material_stock(id, store).quantity(m);
}
void material_add(EntityId id, std::size_t store, const std::string &m, std::int64_t n)
{
	auto &stock = material_stock(id, store);
	if (!(n < 0 ? stock.consume(m, -n) : stock.add(m, n)))
		throw std::runtime_error("Invalid material stock transaction");
}
std::int64_t material_occupied_volume(EntityId id, std::size_t store) const
{
	const auto &stock = material_stock(id, store);
	return stock.occupied() + stock.reserved_capacity();
}
std::int64_t material_space(EntityId id, std::size_t store, const std::string &m) const
{
	return material_accepts(id, store, m) ? material_stock(id, store).headroom(m) : 0;
}
std::int64_t material_pending(EntityId id, std::size_t store, const std::string &m) const
{
	std::int64_t pending = material_quantity(id, store, m);
	for (const auto &[key, claim] : material_stock(id, store).capacity_claims())
		if (key.starts_with("weapon:") && claim.resource == m)
			pending += claim.quantity;
	for (const auto &r : material_requests)
		if (r.destination == id && r.to == store && r.material == m)
			pending += r.remaining;
	for (const auto &b : material_batches)
		if (b.destination == id && b.to == store && b.material == m)
			pending += b.quantity;
	for (const auto &job : cargo_deliveries)
		if (job.collection && job.source == id && job.from == store && job.material == m)
			pending += job.reserved;
		else if (job.target == id && job.to == store && job.material == m)
		{
			auto unrequested = job.reserved;
			for (const auto &request : material_requests)
				if (request.cargo == job.id && request.destination == id && request.to == store)
					unrequested -= std::min(unrequested, request.remaining);
			pending += unrequested;
		}
	return pending;
}
bool return_material_batch(MaterialBatch &batch, bool execute)
{
	if (batch.returning)
		return true;
	if (batch.location != batch.source || batch.crossing ||
	    material_space(batch.source, batch.from, batch.material) < batch.quantity)
		return false;
	if (execute)
	{
		// Move the receiving reservation with the batch; keep its physical transit progress.
		StockTransaction transaction;
		transaction.release_capacity(material_stock(batch.destination, batch.to), material_batch_claim(batch.id));
		if (!transaction.reserve_capacity(material_stock(batch.source, batch.from), material_batch_claim(batch.id),
		                                  batch.material, batch.quantity) ||
		    !transaction.commit())
			return false;
		batch.destination = batch.source;
		batch.to = batch.from;
		batch.returning = true;
	}
	return true;
}
std::size_t material_compartment(EntityId id, std::size_t store) const
{
	const auto &s = material_store(id, store);
	if (!s.system.empty())
		if (const auto fit = instance(entities.at(id), s.system); fit && fit->kind == OperatingSystem::Team)
			return fit->entity.get<CombatMechanism>().get<TeamState>().location;
	return definition(id).combat->compartment_ids.at(s.compartment);
}
bool material_store_working(EntityId id, std::size_t store) const
{
	if (combat_dead(entities.at(id)) || departed(id))
		return false;
	const auto &s = material_store(id, store);
	if (entities.at(id).get<CombatStructure>().compartments.at(material_compartment(id, store)).disabled)
		return false;
	if (!s.system.empty())
	{
		const auto fit = instance(entities.at(id), s.system);
		if (!fit->enabled || !fault_effectiveness(entities.at(id), s.system))
			return false;
		if (fit->kind == OperatingSystem::Team)
		{
			const auto &t = fit->entity.get<CombatMechanism>().get<TeamState>();
			if (t.travel || away_reserved(id, "team:" + s.system))
				return false;
		}
	}
	return true;
}
std::pair<std::int64_t, std::int64_t> material_route(EntityId id, std::size_t from, std::size_t to) const
{
	const auto &s = entities.at(id).get<CombatStructure>();
	return physical_route(id, s.compartments.size(), s.definition.connections, from, to, nullptr, true);
}
bool material_connection(EntityId a, EntityId b) const
{
	return material_gateway(a, b).has_value();
}
void request_material(EntityId source, std::size_t from, EntityId destination, std::size_t to, const std::string &m,
                      std::int64_t n, bool automatic)
{
	if (material_requests.size() >= 4096 || next_material_request >= 9007199254740991ULL)
		return;
	material_requests.push_back({next_material_request++, source, destination, from, to, m, n, automatic});
}
void step_logistics()
{
	const auto rows = inventory_rows();
	for (const auto &[id, entity] : rows)
	{
		const auto *controls = entity.try_get<EntityControls>();
		if (!controls)
			continue;
		for (const auto &controller : controls->controllers)
		{
			const ConsoleAddress owner{id, controller.definition.console};
			if (controller.human || !controls->layout.manual_workloads.contains(owner.console) ||
			    !controls->layout.manual_workloads.at(owner.console).contains("logistics") ||
			    (console_operator(owner) != 0 && manual_workload(owner, "logistics")))
				continue;
			const auto &d = definition(id).logistics;
			for (std::size_t i = 0; i < d.stores.size(); ++i)
				if (d.stores[i].controller == controller.definition.id)
				{
					auto issue = [&](const std::string &verb, const std::string &text)
					{
						OperatingCommand c{id, "logistics." + verb,
						                   static_cast<std::int64_t>(logistics_revision(entity)), d.stores[i].id};
						c.controller = controller.definition.id;
						c.generation = controller.generation;
						c.text = text;
						logistics_command(owner, c, true, true);
					};
					if (entity.get<MaterialInventory>().stores[i].held)
						issue("release", "");
					for (const auto &[material, target] : d.stores[i].targets)
						if (material_stock(id, i).accepts(material) &&
						    (!entity.get<MaterialInventory>().stores[i].targets.contains(material) ||
						     entity.get<MaterialInventory>().stores[i].targets.at(material) != target))
							issue("target", material + "/" + std::to_string(target));
				}
		}
	}
	for (const auto &[id, e] : rows)
	{
		if (combat_dead(e) || departed(id) || craft_dormant(e))
			continue;
		const auto &d = definition(id).logistics;
		for (std::size_t to = 0; to < d.stores.size(); ++to)
			for (const auto &[m, target] : e.get<MaterialInventory>().stores[to].targets)
			{
				if (!material_stock(id, to).accepts(m))
					continue;
				const auto pending = material_pending(id, to, m);
				if (pending >= target)
					continue;
				bool requested = false;
				for (std::size_t from = 0; from < d.stores.size(); ++from)
					if (from != to && d.stores[from].main && material_accepts(id, from, m))
					{
						request_material(id, from, id, to, m, target - pending, true);
						requested = true;
						break;
					}
				if (!requested)
					for (const auto &[source, other] : rows)
						if (source != id && material_connection(source, id))
							for (std::size_t from = 0; from < definition(source).logistics.stores.size(); ++from)
								if (!requested && definition(source).logistics.stores[from].main &&
								    material_accepts(source, from, m))
								{
									request_material(source, from, id, to, m, target - pending, true);
									requested = true;
								}
			}
	}
	std::map<EntityId, std::int64_t> budgets, active;
	for (const auto &[id, e] : rows)
	{
		auto &state = e.get_mut<MaterialInventory>();
		state.handling_remainder += definition(id).logistics.throughput;
		budgets[id] = state.handling_remainder / rate;
		state.handling_remainder %= rate;
	}
	for (const auto &b : material_batches)
		++active[b.source];
	for (auto &r : material_requests)
	{
		if (r.automatic)
		{
			const auto destination = r.destination;
			const auto &local = definition(destination).logistics;
			bool selected = false;
			auto eligible = [&](EntityId id, std::size_t from)
			{
				const auto &d = definition(id).logistics;
				if (!d.stores[from].main || (id == destination && from == r.to) ||
				    !material_accepts(id, from, r.material) ||
				    entities.at(id).get<MaterialInventory>().stores[from].held || !material_store_working(id, from) ||
				    !material_available(id, from, r.material))
					return false;
				return id == destination ? material_compartment(id, from) == material_compartment(destination, r.to) ||
				                               material_route(id, material_compartment(id, from),
				                                              material_compartment(destination, r.to))
				                                       .first >= 0
				                         : material_connection(id, destination);
			};
			for (std::size_t i = 0; i < local.stores.size(); ++i)
				if (!selected && eligible(destination, i))
				{
					r.source = destination;
					r.from = i;
					selected = true;
				}
			for (const auto &[id, e] : rows)
				if (!selected && id != destination)
					for (std::size_t i = 0; i < definition(id).logistics.stores.size(); ++i)
						if (!selected && eligible(id, i))
						{
							r.source = id;
							r.from = i;
							selected = true;
						}
		}

		if (!r.remaining || !material_store_working(r.source, r.from) || !material_store_working(r.destination, r.to))
			continue;
		if (entities.at(r.source).get<MaterialInventory>().stores[r.from].held)
			continue;
		if (material_batches.size() >= 4096 || active[r.source] >= definition(r.source).logistics.concurrency)
			continue;
		const auto start = material_compartment(r.source, r.from), end = material_compartment(r.destination, r.to);
		if (r.source == r.destination ? start != end && material_route(r.source, start, end).first < 0
		                              : !material_connection(r.source, r.destination))
			continue;
		std::int64_t cargo_credit = 0;
		for (const auto &job : cargo_deliveries)
			if (job.id == r.cargo)
			{
				if (job.collection && r.destination == job.target && r.material == job.payment)
				{
					cargo_credit = job.payment_amount - job.paid;
					for (const auto &batch : material_batches)
						if (batch.cargo == job.id && batch.destination == job.target && batch.material == job.payment)
							cargo_credit -= batch.quantity;
				}
				else if (job.collection ? r.destination == job.source && r.material == job.material
				                        : r.source == job.carrier && r.destination == job.target)
					cargo_credit = job.reserved;
			}
		if (r.source != r.destination)
		{
			const auto gateway = material_gateway(r.source, r.destination);
			if (!gateway || (start != gateway->from && material_route(r.source, start, gateway->from).first < 0) ||
			    (end != gateway->to && material_route(r.destination, gateway->to, end).first < 0))
				continue;
		}
		auto n = std::min({r.remaining, material_available(r.source, r.from, r.material, r.cargo),
		                   material_space(r.destination, r.to, r.material) + cargo_credit, budgets[r.source],
		                   definition(r.source).logistics.batch_size});
		if (r.source != r.destination)
			n = std::min(n, budgets[r.destination]);
		if (n <= 0)
			continue;
		StockTransaction transaction;
		if (!stage_material_dispatch(transaction, r, n, next_material_batch) || !transaction.commit())
			continue;
		for (auto &job : cargo_deliveries)
			if (job.id == r.cargo && job.collection)
			{
				if (r.source == job.source && r.material == job.payment)
					job.payment_taken += n;
				if (r.source == job.target && r.material == job.material)
					job.goods_taken += n;
			}
		r.remaining -= n;
		if (cargo_credit)
			for (auto &job : cargo_deliveries)
				if (job.id == r.cargo && (!job.collection || r.destination == job.source))
					job.reserved -= std::min(job.reserved, n);
		budgets[r.source] -= n;
		if (r.source != r.destination)
			budgets[r.destination] -= n;
		material_batches.push_back(
		    {next_material_batch++, r.id, r.source, r.destination, r.source, r.from, r.to, start, r.material, n});
		material_batches.back().cargo = r.cargo;
		++active[r.source];
	}
	std::erase_if(material_requests,
	              [](const auto &r)
	              {
		              return r.remaining == 0;
	              });

	std::map<std::pair<EntityId, EntityId>, std::int64_t> crossing_budgets;
	for (auto &b : material_batches)
	{
		if (combat_dead(entities.at(b.location)) || departed(b.location))
			continue;
		if (b.crossing)
		{
			const auto g = material_gateway(b.location, b.destination, true);
			if (!g)
				continue;
			const auto key = std::minmax(b.location, b.destination);
			auto [at, inserted] = crossing_budgets.try_emplace({key.first, key.second}, g->throughput);
			const auto work = std::min(b.crossing, at->second);
			b.crossing -= work;
			at->second -= work;
			if (b.crossing)
				continue;
			b.location = b.destination;
			b.compartment = g->to;
		}
		if (b.travel)
		{
			const auto &structure = entities.at(b.location).get<CombatStructure>();
			if (!physical_edge_open(b.location, b.compartment, b.edge_to) ||
			    structure.compartments.at(b.compartment).disabled || structure.compartments.at(b.edge_to).disabled)
				continue;
			if (--b.travel)
				continue;
			b.compartment = b.edge_to;
			b.edge_to = -1;
		}
		const auto gateway = b.location == b.destination ? std::optional<MaterialGateway>{}
		                                                 : material_gateway(b.location, b.destination);
		if (b.location != b.destination && !gateway)
			continue;
		const auto end = b.location == b.destination ? material_compartment(b.destination, b.to) : gateway->from;
		if (b.compartment != end)
		{
			const auto route = material_route(b.location, b.compartment, end);
			if (route.first < 0)
				continue;
			b.edge_to = route.first;
			b.travel = route.second;
			continue;
		}
		if (b.location != b.destination)
		{
			b.crossing = b.quantity * rate;
			continue;
		}
		if (!material_store_working(b.destination, b.to))
			continue;
		if (!material_stock(b.destination, b.to).receive_reserved(material_batch_claim(b.id)))
			throw std::runtime_error("Missing delivery receiving commitment");
		for (auto &job : cargo_deliveries)
			if (job.id == b.cargo && job.collection && !b.returning)
			{
				if (b.destination == job.target && b.material == job.payment)
					job.paid += b.quantity;
				if (b.destination == job.carrier && b.material == job.material)
					job.collected += b.quantity;
				if (b.destination == job.source && b.material == job.material)
					job.delivered += b.quantity;
			}
		b.quantity = 0;
		for (const auto &job : cargo_deliveries)
			if (job.id == b.cargo)
				sync_cargo_stock(job);
	}

	std::erase_if(material_batches,
	              [](const auto &b)
	              {
		              return b.quantity == 0;
	              });
}
bool material_authority(const ConsoleAddress &a, const OperatingCommand &c, const std::string &owner, bool ai) const
{
	if (!entities.at(a.ship).has<EntityControls>())
		return false;
	for (const auto &ctrl : entities.at(a.ship).get<EntityControls>().controllers)
		if (ctrl.definition.console == a.console && ctrl.definition.id == owner && c.controller == owner &&
		    ctrl.generation == c.generation && ctrl.human != ai)
			return true;
	return false;
}
bool logistics_command(const ConsoleAddress &a, const OperatingCommand &c, bool execute, bool ai = false)
{
	const auto e = entities.at(a.ship);
	if (!ai && !manual_workload(a, "logistics"))
		return false;
	if (c.channel != "" || c.request_id || !c.arguments.empty() || c.text.size() > 512 || !c.speaker_name.empty() ||
	    !c.speaker_avatar.empty())
		return false;
	const auto revision = logistics_revision(e);
	if (c.value != static_cast<std::int64_t>(revision) || revision >= 9007199254740991ULL)
		return false;
	if (c.action == "logistics.route")
		return compartment_access_command(a, c, execute, ai);
	if (!e.has<MaterialInventory>())
		return false;
	auto &state = e.get_mut<MaterialInventory>();
	const auto &d = definition(a.ship).logistics;
	if (c.action == "logistics.deliver" || c.action == "logistics.craft_allow" || c.action == "logistics.cargo_cancel")
		return cargo_command(a, c, execute, ai);
	if (c.action == "logistics.cancel" && c.system.starts_with("batch:"))
	{
		if (!material_authority(a, c, d.controller, ai))
			return false;
		auto it = std::find_if(material_batches.begin(), material_batches.end(),
		                       [&](const auto &b)
		                       {
			                       return b.source == a.ship && c.system == "batch:" + std::to_string(b.id);
		                       });
		if (it == material_batches.end() || !return_material_batch(*it, execute))
			return false;
		if (execute)
			++state.revision;
		return true;
	}
	if (c.action == "logistics.cancel")
	{
		if (!material_authority(a, c, d.controller, ai))
			return false;
		const auto it = std::find_if(material_requests.begin(), material_requests.end(),
		                             [&](const auto &r)
		                             {
			                             return r.source == a.ship && c.system == std::to_string(r.id);
		                             });
		if (it == material_requests.end())
			return false;
		if (execute)
		{
			material_requests.erase(it);
			++state.revision;
		}
		return true;
	}
	std::size_t index = d.stores.size();
	for (std::size_t i = 0; i < d.stores.size(); ++i)
		if (d.stores[i].id == c.system)
			index = i;
	if (index == d.stores.size())
		return false;
	const auto &s = d.stores[index];
	if (!material_authority(a, c, s.controller, ai))
		return false;
	if (c.action == "logistics.transfer")
	{
		const auto fields = material_fields(c.text);
		if (fields.size() != 4 || material_requests.size() >= 4096)
			return false;
		const auto number = parse_material_number(fields[0]), n = parse_material_number(fields[3]);
		if (number < 1 || n < 1)
			return false;
		const auto target = static_cast<EntityId>(number);
		if (!entities.contains(target) || !entities.at(target).has<MaterialInventory>() || target == a.ship ||
		    !material_connection(a.ship, target))
			return false;
		const auto &remote = definition(target).logistics;
		std::size_t to = remote.stores.size();
		for (std::size_t i = 0; i < remote.stores.size(); ++i)
			if (remote.stores[i].id == fields[1])
				to = i;
		if (to == remote.stores.size() || !material_accepts(a.ship, index, fields[2]) ||
		    !material_accepts(target, to, fields[2]) || n > 1000000000)
			return false;
		if (execute)
		{
			request_material(a.ship, index, target, to, fields[2], n, false);
			++state.revision;
		}
		return true;
	}
	if (c.action == "logistics.hold" || c.action == "logistics.release")
	{
		if (!s.main || !c.text.empty())
			return false;
		if (execute)
		{
			state.stores[index].held = c.action == "logistics.hold";
			++state.revision;
		}
		return true;
	}
	if (c.action == "logistics.select")
		return select_physical_resource(e, state.stores[index].stock, c.text, execute);
	if (c.action == "logistics.target")
	{
		const auto slash = c.text.find('/');
		if (slash == std::string::npos)
			return false;
		const auto m = c.text.substr(0, slash);
		const auto n = parse_material_number(c.text.substr(slash + 1));
		if (n < 0 || !material_accepts(a.ship, index, m) || n > s.capacity / material_size(a.ship, index, m))
			return false;
		if (execute)
		{
			state.stores[index].targets[m] = n;
			std::erase_if(material_requests,
			              [&](const auto &r)
			              {
				              return r.automatic && r.destination == a.ship && r.to == index && r.material == m;
			              });
			++state.revision;
		}
		return true;
	}
	return false;
}
std::int64_t parse_material_number(const std::string &s) const
{
	if (s.empty() || s.size() > 15)
		return -1;
	std::int64_t n = 0;
	for (char c : s)
	{
		if (c < '0' || c > '9')
			return -1;
		n = n * 10 + c - '0';
	}
	return n;
}
Json logistics_board(const ConsoleAddress &a) const
{
	Json out{{"revision", 1},
	         {"stores", Json::array()},
	         {"requests", Json::array()},
	         {"batches", Json::array()},
	         {"cargo", Json::array()},
	         {"cargo_craft", Json::array()},
	         {"offers", Json::array()},
	         {"routes", Json::array()},
	         {"controller", ""},
	         {"route_controller", ""}};
	const auto e = entities.at(a.ship);
	out["revision"] = logistics_revision(e);
	out["manual"] = manual_workload(a, "logistics");
	auto visible = [&](const std::string &owner)
	{
		if (owner.empty() || !e.has<EntityControls>())
			return false;
		for (const auto &ctrl : e.get<EntityControls>().controllers)
			if (ctrl.definition.id == owner)
				return ctrl.definition.console == a.console;
		return false;
	};
	if (const auto *structure = e.try_get<CombatStructure>())
	{
		out["route_controller"] = structure->definition.access_controller;
		if (visible(structure->definition.access_controller))
			for (const auto &route : structure->definition.connections)
				out["routes"].push_back({{"from", structure->definition.compartments[route.from].id},
				                         {"to", structure->definition.compartments[route.to].id},
				                         {"closed", !physical_edge_open(a.ship, route.from, route.to)}});
	}
	if (!e.has<MaterialInventory>())
		return out;
	const auto &d = definition(a.ship).logistics;
	const auto &state = e.get<MaterialInventory>();
	const bool main = visible(d.controller);
	out["controller"] = d.controller;
	for (std::size_t i = 0; i < d.stores.size(); ++i)
	{
		const auto &s = d.stores[i];
		if (!main && !visible(s.controller))
			continue;
		Json quantities = Json::object(), space = Json::object();
		Json selectable = Json::array(), labels = Json::object();
		for (const auto &m : s.accepts)
		{
			quantities[m] = material_quantity(a.ship, i, m);
			space[m] = material_space(a.ship, i, m);
			labels[m] = material_stock(a.ship, i).catalogue().at(m).label;
			auto candidate = material_stock(a.ship, i);
			if (candidate.select(m))
				selectable.push_back(m);
		}
		out["stores"].push_back({{"id", s.id},
		                         {"controller", s.controller},
		                         {"main", s.main},
		                         {"held", state.stores[i].held},
		                         {"quantities", quantities},
		                         {"space", space},
		                         {"targets", state.stores[i].targets},
		                         {"working", material_store_working(a.ship, i)},
		                         {"single_resource", material_stock(a.ship, i).definition().single_resource},
		                         {"selected_resource", material_stock(a.ship, i).selected()},
		                         {"resource_choices", s.accepts},
		                         {"resource_labels", labels},
		                         {"selectable_resources", selectable}});
	}
	for (const auto &[id, craft] : craft_rows())
		if (craft.get<CraftState>().home == a.ship && definition(id).craft->role == "shuttle")
		{
			const auto &bay = craft.get<CraftState>().bay;
			if (main || enabled_system_owner(a.ship, bay) == a)
			{
				Json stores = Json::array();
				for (const auto &store : definition(id).logistics.stores)
					stores.push_back({{"id", store.id}, {"accepts", store.accepts}});
				out["cargo_craft"].push_back(
				    {{"id", id},
				     {"authorised", state.authorised_craft.contains(id)},
				     {"controllable", manual_workload(a, "logistics") && enabled_system_owner(a.ship, bay) == a},
				     {"available", craft_available(id)},
				     {"stores", stores}});
			}
		}
	if (main)
	{
		for (const auto &job : cargo_deliveries)
			if (job.source == a.ship)
				out["cargo"].push_back({{"id", job.id},
				                        {"carrier", job.carrier},
				                        {"target", job.target},
				                        {"material", job.material},
				                        {"quantity", job.amount},
				                        {"phase", job.report},
				                        {"report_tick", job.report_tick}});
		for (const auto &[target, other] : inventory_rows())
			if (target != a.ship && (material_connection(a.ship, target) || dock_known(a, target)))
				for (std::size_t i = 0; i < definition(target).logistics.stores.size(); ++i)
				{
					const auto &store = definition(target).logistics.stores[i];
					if (!store.main)
						continue;
					for (const auto &m : store.accepts)
						if (out["offers"].size() < 128)
							out["offers"].push_back({{"entity", target},
							                         {"store", store.id},
							                         {"material", m},
							                         {"connected", material_connection(a.ship, target)}});
					if (out["offers"].size() >= 128)
						break;
				}
	}
	if (main)
	{
		for (const auto &r : material_requests)
			if ((r.source == a.ship || r.destination == a.ship) && out["requests"].size() < 256)
				out["requests"].push_back(r);
		for (const auto &b : material_batches)
			if ((b.source == a.ship || b.destination == a.ship) && out["batches"].size() < 256)
				out["batches"].push_back(b);
	}
	return out;
}
void restore_logistics(const Json &saved)
{
	material_requests.clear();
	material_batches.clear();
	const auto &j = saved.at("logistics");
	material_requests = j.at("requests").get<std::vector<MaterialRequest>>();
	material_batches = j.at("batches").get<std::vector<MaterialBatch>>();
	cargo_deliveries = j.value("cargo", Json::array()).get<std::vector<CargoDelivery>>();
	next_cargo_delivery = j.value("next_cargo", std::uint64_t{1});
	next_material_request = j.at("next_request");
	next_material_batch = j.at("next_batch");
	if (material_requests.size() > 4096 || material_batches.size() > 4096 || !next_material_request ||
	    !next_material_batch)
		throw std::runtime_error("Invalid logistics continuation size");
	for (const auto &b : saved.at("bodies"))
	{
		const auto id = b.at("body").at("id").get<EntityId>();
		const auto e = entities.at(id);
		if (!e.has<MaterialInventory>())
		{
			if (b.contains("inventory"))
				throw std::runtime_error("Unexpected inventory");
			continue;
		}
		for (const auto &saved_store : b.at("inventory").at("stores"))
			if (!saved_store.is_object() || saved_store.size() != 3 || !saved_store.contains("stock") ||
			    !saved_store.contains("targets") || !saved_store.contains("held"))
				throw std::runtime_error("Invalid inventory store continuation fields");
		const auto state = b.at("inventory").get<MaterialInventory>();
		const auto &d = definition(id).logistics;
		if (state.stores.size() != d.stores.size() || !state.revision || state.revision >= 9007199254740991ULL ||
		    state.handling_remainder < 0 || state.handling_remainder >= rate)
			throw std::runtime_error("Invalid inventory continuation");
		for (std::size_t i = 0; i < state.stores.size(); ++i)
		{
			if (state.stores[i].stock != e.get<MaterialInventory>().stores[i].stock)
				throw std::runtime_error("Invalid inventory stock binding");
			for (const auto &[m, n] : state.stores[i].targets)
				if (!material_accepts(id, i, m) || n < 0 || n > d.stores[i].capacity / material_size(id, i, m))
					throw std::runtime_error("Invalid saved material target");
		}
		if (state.work_credit.size() > d.consumers.size())
			throw std::runtime_error("Invalid saved work credit");
		for (const auto &[key, n] : state.work_credit)
			if (!material_consumer(id, key) || n < 0 || n > rate * 1000)
				throw std::runtime_error("Invalid saved material credit");
		e.set<MaterialInventory>(state);
	}
	auto endpoint = [&](EntityId id, std::size_t i, const std::string &m)
	{
		return entities.contains(id) && entities.at(id).has<MaterialInventory>() &&
		       i < definition(id).logistics.stores.size() && material_accepts(id, i, m);
	};
	std::uint64_t last = 0;
	for (const auto &r : material_requests)
	{
		if (r.id <= last || r.id >= next_material_request || r.remaining <= 0 ||
		    !endpoint(r.source, r.from, r.material) || !endpoint(r.destination, r.to, r.material))
			throw std::runtime_error("Invalid saved supply request");
		last = r.id;
	}
	last = 0;
	for (const auto &b : material_batches)
	{
		if (b.id <= last || b.id >= next_material_batch || !b.request || b.request >= next_material_request ||
		    b.quantity <= 0 || !endpoint(b.source, b.from, b.material) || !endpoint(b.destination, b.to, b.material) ||
		    (b.location != b.source && b.location != b.destination) ||
		    b.compartment >= definition(b.location).combat->compartments.size() || b.edge_to < -1 ||
		    b.edge_to >= static_cast<std::int64_t>(definition(b.location).combat->compartments.size()) ||
		    b.crossing < 0 || b.crossing > 1000000000 || b.travel < 0 || (b.travel > 0) != (b.edge_to >= 0))
			throw std::runtime_error("Invalid saved delivery batch");
		if (b.crossing && (b.travel || b.location == b.destination))
			throw std::runtime_error("Invalid crossing location");
		if (b.returning && (b.destination != b.source || b.to != b.from))
			throw std::runtime_error("Invalid returned delivery destination");
		if (b.travel)
		{
			bool edge = false;
			for (const auto &link : definition(b.location).combat->connections)
				edge |= ((link.from == b.compartment && link.to == b.edge_to) ||
				         (link.to == b.compartment && link.from == b.edge_to)) &&
				        b.travel <= link.ticks;
			if (!edge)
				throw std::runtime_error("Invalid saved material route progress");
		}
		last = b.id;
	}
	validate_cargo();
	// Persisted stock reservations must exactly match native delivery/cargo owners.
	StockTransaction commitments;
	std::set<StockStore *> stores;
	for (const auto &[id, entity] : entities)
		if (auto *physical = entity.try_get_mut<ShipStockStores>())
			for (auto &[key, stock] : physical->stores)
				stores.insert(&stock);
	for (auto *stock : stores)
	{
		for (const auto &[key, claim] : stock->capacity_claims())
		{
			if (key.starts_with("batch:") || key.starts_with("cargo:"))
				commitments.release_capacity(*stock, key);
			else if (!key.starts_with("weapon:") && !key.starts_with("processor:"))
				throw std::runtime_error("Unknown physical receiving commitment owner");
		}
		for (const auto &[key, claim] : stock->stock_claims())
		{
			if (!key.starts_with("cargo:"))
				throw std::runtime_error("Unknown physical stock commitment owner");
			commitments.release_stock(*stock, key);
		}
	}
	for (const auto &batch : material_batches)
		if (!commitments.reserve_capacity(material_stock(batch.destination, batch.to), material_batch_claim(batch.id),
		                                  batch.material, batch.quantity))
			throw std::runtime_error("Invalid saved delivery commitment");
	for (const auto &job : cargo_deliveries)
		if (!stage_cargo_claims(commitments, job))
			throw std::runtime_error("Invalid saved cargo stock commitment");
	for (auto *stock : stores)
		if (stock->capacity_claims() != commitments.view(*stock).capacity_claims() ||
		    stock->stock_claims() != commitments.view(*stock).stock_claims())
			throw std::runtime_error("Orphan or mismatched saved physical commitment");
	for (const auto &[id, e] : inventory_rows())
		for (std::size_t i = 0; i < definition(id).logistics.stores.size(); ++i)
		{
			const auto used = material_occupied_volume(id, i);
			if (used > material_store(id, i).capacity)
				throw std::runtime_error("Saved stock and reservations exceed capacity");
		}
}

const MaterialConsumerDefinition *material_consumer(EntityId origin, const std::string &system) const
{
	for (const auto &c : definition(origin).logistics.consumers)
		if (c.system == system)
			return &c;
	return nullptr;
}
bool pay_material_work(EntityId origin, const std::string &system, const std::string &activity, bool paid)
{
	if (paid)
		return true;
	const auto *c = material_consumer(origin, system);
	if (!c || !c->costs.contains(activity))
		return true;
	const auto &d = definition(origin).logistics;
	const auto at = std::find_if(d.stores.begin(), d.stores.end(),
	                             [&](const auto &s)
	                             {
		                             return s.id == c->store;
	                             });
	const auto index = static_cast<std::size_t>(at - d.stores.begin());
	// Deployed teams carry their own kit; stationary stores cannot supply them remotely.
	if (away_reserved(origin, "team:" + system) && at->system != system)
		return false;
	if (!at->system.empty())
	{
		const auto fit = instance(entities.at(origin), at->system);
		if (!fit || !fit->enabled || !fault_effectiveness(entities.at(origin), at->system))
			return false;
	}
	const auto &costs = c->costs.at(activity);
	for (const auto &[m, n] : costs)
		if (material_stock(origin, index).available(m) < n)
			return false;
	StockTransaction transaction;
	for (const auto &[m, n] : costs)
		transaction.consume(material_stock(origin, index), m, n);
	return transaction.commit();
}
std::int64_t paid_structural_work(EntityId origin, const std::string &system, std::int64_t work)
{
	const auto *c = material_consumer(origin, system);
	if (!c || !c->costs.contains("structural"))
		return work;
	auto &credit = entities.at(origin).get_mut<MaterialInventory>().work_credit[system];
	std::int64_t result = 0;
	while (work > 0)
	{
		if (!credit)
		{
			if (!pay_material_work(origin, system, "structural", false))
				break;
			credit = rate * 1000;
		}
		const auto amount = std::min(work, credit);
		credit -= amount;
		work -= amount;
		result += amount;
	}
	return result;
}

std::vector<std::string> material_fields(const std::string &text) const
{
	std::vector<std::string> out;
	std::size_t begin = 0;
	for (std::size_t i = 0; i <= text.size(); ++i)
		if (i == text.size() || text[i] == '/')
		{
			out.push_back(text.substr(begin, i - begin));
			begin = i + 1;
		}
	return out;
}
