module;
#include "native_headers.hpp"

module phoenix.simulation;
import :travel_bindings;

namespace phoenix::world_detail
{
auto Runtime::cargo_committed(EntityId craft) const -> bool
{
	for (const auto &job : cargo_deliveries)
		if (job.carrier == craft && job.phase != "complete" && job.phase != "lost")
			return true;
	return false;
}

auto Runtime::clear_cargo_claims(StockTransaction &transaction, const CargoDelivery &job) const -> void
{
	phoenix::clear_cargo_claims(transaction, job,
	                            {[&](EntityId id, std::size_t store) -> StockStore &
	                             {
		                             return material_stock(id, store);
	                             }});
}

auto Runtime::stage_cargo_claims(StockTransaction &transaction, const CargoDelivery &job,
                                 const MaterialBatch *dispatch) const -> bool
{
	return phoenix::stage_cargo_claims(transaction, job, material_batches,
	                                   {[&](EntityId id, std::size_t store) -> StockStore &
	                                    {
		                                    return material_stock(id, store);
	                                    }},
	                                   dispatch);
}

auto Runtime::sync_cargo_stock(const CargoDelivery &job) -> void
{
	StockTransaction transaction;
	clear_cargo_claims(transaction, job);
	if (!stage_cargo_claims(transaction, job) || !transaction.commit())
		throw std::runtime_error("Invalid cargo stock commitment");
}

auto Runtime::material_available(EntityId id, std::size_t store, const std::string &material,
                                 std::uint64_t exempt_cargo) const -> std::int64_t
{
	return available_material(material_stock(id, store), material, exempt_cargo);
}

auto Runtime::stage_material_dispatch(StockTransaction &transaction, const MaterialRequest &request,
                                      std::int64_t quantity, std::uint64_t batch_id) -> bool
{
	return phoenix::stage_material_dispatch(transaction, request, quantity, batch_id, cargo_deliveries,
	                                        material_batches,
	                                        {[&](EntityId id, std::size_t store) -> StockStore &
	                                         {
		                                         return material_stock(id, store);
	                                         }});
}

auto Runtime::cargo_authorisation_console(const ConsoleAddress &a, bool human) const -> bool
{
	const auto &owner = definition(a.ship).logistics.cargo_authorisation_controller;
	if (owner.empty())
		return false;
	for (const auto &controller : entities.at(a.ship).get<EntityControls>().controllers)
		if (controller.definition.id == owner && controller.definition.console == a.console &&
		    (!human || controller.human))
			return true;
	return false;
}

auto Runtime::cargo_authority(const ConsoleAddress &a, const OperatingCommand &c, EntityId craft, bool ai) const -> bool
{
	const auto &state = entities.at(craft).get<CraftState>();
	if (state.home != a.ship)
		return false;
	const auto &delegate = definition(a.ship).logistics.cargo_authorisation_controller;
	if (!delegate.empty())
		return material_authority(a, c, delegate, ai);
	if (enabled_system_owner(a.ship, state.bay) != a)
		return false;
	for (const auto &ctrl : entities.at(a.ship).get<EntityControls>().controllers)
		if (material_authority(a, c, ctrl.definition.id, ai))
			for (const auto &ch : ctrl.definition.commands)
				for (auto consumer : ch.consumers)
					if (definition(a.ship).fitted->values[consumer].id == state.bay)
						return true;
	return false;
}

auto Runtime::cargo_command(const ConsoleAddress &a, const OperatingCommand &c, bool execute, bool ai) -> bool
{
	const auto e = entities.at(a.ship);
	if (!e.has<MaterialInventory>())
		return false;
	auto &state = e.get_mut<MaterialInventory>();
	if (c.action == "logistics.craft_allow")
	{
		const auto id = parse_material_number(c.system);
		if (id < 1 || !entities.contains(id) || !entities.at(id).has<CraftState>() ||
		    (c.text != "0" && c.text != "1") || !cargo_authority(a, c, id, ai))
			return false;
		if (execute)
		{
			if (c.text == "1")
				state.authorised_craft.insert(id);
			else
				state.authorised_craft.erase(id);
			++state.revision;
		}
		return true;
	}
	if (c.action == "logistics.cargo_cancel")
	{
		const auto id = parse_material_number(c.system);
		auto it = std::find_if(cargo_deliveries.begin(), cargo_deliveries.end(),
		                       [&](const auto &job)
		                       {
			                       return job.id == id && job.source == a.ship;
		                       });
		if (it == cargo_deliveries.end() || !material_authority(a, c, definition(a.ship).logistics.controller, ai))
			return false;
		if (execute)
		{
			request_cargo_return(*it);
			++state.revision;
		}
		return true;
	}
	if (c.action != "logistics.deliver" || material_requests.size() >= 4096)
		return false;
	const auto &d = definition(a.ship).logistics;
	if (!material_authority(a, c, d.controller, ai) || cargo_deliveries.size() >= 128)
		return false;
	const auto fields = material_fields(c.text);
	if (fields.size() != 6)
		return false;
	const auto craft_number = parse_material_number(fields[0]), target_number = parse_material_number(fields[2]),
	           amount = parse_material_number(fields[5]);
	if (craft_number < 1 || target_number < 1 || amount < 1 || amount > 1000000000)
		return false;
	const auto craft = static_cast<EntityId>(craft_number), target = static_cast<EntityId>(target_number);
	if (!entities.contains(craft) || !entities.contains(target) || !entities.at(craft).has<CraftState>() ||
	    !entities.at(craft).has<MaterialInventory>() || !entities.at(target).has<MaterialInventory>() ||
	    target == a.ship || target == craft || craft_dormant(entities.at(target)) || !dock_known(a, target))
		return false;
	const auto &cs = entities.at(craft).get<CraftState>();
	if (cs.home != a.ship || definition(craft).craft->role != "shuttle" || !craft_available(craft) ||
	    !state.authorised_craft.contains(craft))
		return false;
	auto store = [&](EntityId id, const std::string &key)
	{
		const auto &stores = definition(id).logistics.stores;
		for (std::size_t i = 0; i < stores.size(); ++i)
			if (stores[i].id == key)
				return i;
		return stores.size();
	};
	const auto from = store(a.ship, c.system), cargo = store(craft, fields[1]), to = store(target, fields[3]);
	const auto &m = fields[4];
	if (from == d.stores.size() || cargo == definition(craft).logistics.stores.size() ||
	    to == definition(target).logistics.stores.size() || !material_accepts(a.ship, from, m) ||
	    !material_accepts(craft, cargo, m) || !material_accepts(target, to, m) ||
	    material_space(craft, cargo, m) < amount)
		return false;
	for (const auto &type : material_store(craft, cargo).accepts)
		if (material_quantity(craft, cargo, type))
			return false;
	for (const auto &request : material_requests)
		if (request.destination == craft && request.to == cargo)
			return false;
	for (const auto &batch : material_batches)
		if (batch.destination == craft && batch.to == cargo)
			return false;
	bool port = false;
	for (std::size_t p = 0; p < definition(craft).docking.size(); ++p)
		for (std::size_t q = 0; q < definition(target).docking.size(); ++q)
			if (definition(craft).docking[p].compatibility == definition(target).docking[q].compatibility)
				port = true;
	if (!port)
		return false;
	if (execute)
	{
		CargoDelivery job;
		job.id = next_cargo_delivery++;
		job.source = a.ship;
		job.carrier = craft;
		job.target = target;
		job.from = from;
		job.cargo = cargo;
		job.to = to;
		job.material = m;
		job.amount = amount;
		request_material(a.ship, from, craft, cargo, m, amount, false);
		material_requests.back().cargo = job.id;
		cargo_deliveries.push_back(job);
		sync_cargo_stock(cargo_deliveries.back());
		++state.revision;
	}
	return true;
}

auto Runtime::step_cargo() -> void
{
	for (auto &job : cargo_deliveries)
	{
		if (job.collection)
		{
			step_supply_cargo(job);
			continue;
		}
		const auto e = entities.at(job.carrier);
		if (combat_dead(e) || e.get<CraftState>().phase == CraftPhase::Lost)
		{
			job.phase = "lost";
			job.reserved = 0;
			sync_cargo_stock(job);
		}
		if (craft_link(job.carrier))
		{
			job.report = job.phase;
			job.report_tick = ecs.get<Clock>().tick;
		}
		if (job.phase == "complete" || job.phase == "lost")
			continue;
		if ((job.phase == "outbound" || job.phase == "unloading") && e.get<CraftState>().order == "return")
			request_cargo_return(job);
		if (job.phase == "loading")
		{
			if (material_quantity(job.carrier, job.cargo, job.material) < job.amount)
				continue;
			const auto *owner = comms_owner(job.source).has_value() ? &definition(job.source) : nullptr;
			if (material_space(job.target, job.to, job.material) < job.amount || !craft_link(job.carrier) ||
			    ftl_preparing(job.source) || !owner)
				continue;
			const auto address = *comms_owner(job.source);
			if (!dock_report_available(address, job.target))
				continue;
			job.reserved = job.amount;
			job.phase = "outbound";
			sync_cargo_stock(job);
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
				continue;
			if (!e.has<DockConnection>())
			{
				for (std::size_t p = 0; p < definition(job.carrier).docking.size() && !e.has<DockConnection>(); ++p)
					for (std::size_t q = 0; q < definition(job.target).docking.size() && !e.has<DockConnection>(); ++q)
					{
						const auto &port = definition(job.carrier).docking[p];
						for (const auto &ctrl : e.get<EntityControls>().controllers)
							if (ctrl.definition.id == port.controller)
							{
								OperatingCommand c{job.carrier, "dock.assist",
								                   static_cast<std::int64_t>(e.get<DockingState>().revision), port.id};
								c.controller = ctrl.definition.id;
								c.generation = ctrl.generation;
								c.text = std::to_string(job.target) + "/" + definition(job.target).docking[q].id;
								docking_command({job.carrier, ctrl.definition.console}, c, true, true);
							}
					}
			}
			if (dock_ready(job.carrier, job.target))
			{
				if (material_requests.size() >= 4096)
					continue;
				job.phase = "unloading";
				request_material(job.carrier, job.cargo, job.target, job.to, job.material, job.amount, false);
				material_requests.back().cargo = job.id;
			}
		}
		if (job.phase == "unloading")
		{
			bool pending = false;
			for (const auto &r : material_requests)
				pending |= r.cargo == job.id;
			for (const auto &b : material_batches)
				pending |= b.source == job.carrier && b.destination == job.target;
			if (!pending)
			{
				job.phase = "returning";
				job.reserved = 0;
				sync_cargo_stock(job);
				if (e.has<DockConnection>())
				{
					auto &c = e.get_mut<DockConnection>();
					c.phase = "separating";
					c.progress = 0;
				}
				craft_return(e);
			}
		}
		if (job.phase == "returning" && e.get<CraftState>().phase == CraftPhase::Docked)
		{
			// Unused cargo remains physically aboard; a later transfer can unload it.
			bool pending = false;
			for (const auto &b : material_batches)
				pending |= b.destination == job.carrier;
			if (!pending)
			{
				job.phase = "complete";
				sync_cargo_stock(job);
			}
		}
	}
}

auto Runtime::validate_cargo() const -> void
{
	if (cargo_deliveries.size() > 128 || !next_cargo_delivery || next_cargo_delivery > 9007199254740991ULL)
		throw std::runtime_error("Invalid cargo continuation count");
	std::set<EntityId> committed;
	std::uint64_t previous = 0;
	for (const auto &job : cargo_deliveries)
	{
		if (!job.id || job.id <= previous || job.id >= next_cargo_delivery || !entities.contains(job.source) ||
		    !entities.contains(job.carrier) || !entities.contains(job.target) ||
		    !entities.at(job.carrier).has<CraftState>() ||
		    entities.at(job.carrier).get<CraftState>().home != job.source || job.source == job.target ||
		    job.amount <= 0 || job.amount > 1000000000 || job.reserved < 0 || job.reserved > job.amount ||
		    job.report_tick > ecs.get<Clock>().tick)
			throw std::runtime_error("Invalid cargo commitment");
		previous = job.id;
		if (job.phase != "loading" && job.phase != "outbound" && job.phase != "unloading" && job.phase != "returning" &&
		    !(job.collection && (job.phase == "paying" || job.phase == "collecting" || job.phase == "cancelling" ||
		                         job.phase == "home-unloading")) &&
		    job.phase != "complete" && job.phase != "lost")
			throw std::runtime_error("Invalid cargo phase");
		if (job.reserved && !job.collection && job.phase != "outbound" && job.phase != "unloading")
			throw std::runtime_error("Invalid cargo reservation phase");
		for (const auto &[id, index] : std::vector<std::pair<EntityId, std::size_t>>{
		         {job.source, job.from}, {job.carrier, job.cargo}, {job.target, job.to}})
			if (!entities.at(id).has<MaterialInventory>() || index >= definition(id).logistics.stores.size() ||
			    !material_accepts(id, index, job.material))
				throw std::runtime_error("Invalid cargo store");
		if (job.phase != "complete" && job.phase != "lost" &&
		    (!committed.insert(job.carrier).second ||
		     (!entities.at(job.carrier).get<CraftState>().mission.empty() &&
		      (!job.collection || entities.at(job.carrier).get<CraftState>().mission != job.mission))))
			throw std::runtime_error("Double-booked cargo craft");
		if (job.collection &&
		    (job.mission.empty() || job.payment_amount < 0 || job.payment_amount > 1000000000 ||
		     job.payment_taken < 0 || job.payment_taken > job.payment_amount || job.paid < 0 ||
		     job.paid > job.payment_taken || job.goods_taken < 0 || job.goods_taken > job.amount || job.collected < 0 ||
		     job.collected > job.goods_taken || job.delivered < 0 || job.delivered > job.collected))
			throw std::runtime_error("Invalid supply receipts");
		if (job.collection)
		{
			bool found = false;
			for (std::size_t i = 0; i < away_operations.missions().size(); ++i)
				if (away_operations.definitions()[i].id == job.mission &&
				    away_ship(away_operations.definitions()[i]) == job.source &&
				    away_operations.definitions()[i].supply && away_operations.missions()[i].supply_job == job.id &&
				    away_transport_entity(job.source, away_operations.missions()[i].transport) == job.carrier)
					found = true;
			if (!found)
				throw std::runtime_error("Supply commitment without owning mission");
			if (!job.payment.empty())
				for (const auto &[id, index] : std::vector<std::pair<EntityId, std::size_t>>{
				         {job.source, job.from}, {job.carrier, job.cargo}, {job.target, job.to}})
					if (!material_accepts(id, index, job.payment))
						throw std::runtime_error("Invalid supply payment store");
		}
	}
	for (const auto &r : material_requests)
		if (r.cargo)
		{
			bool found = false;
			for (const auto &job : cargo_deliveries)
				found |= job.id == r.cargo;
			if (!found)
				throw std::runtime_error("Cargo request without commitment");
		}
	for (const auto &batch : material_batches)
		if (batch.cargo && std::none_of(cargo_deliveries.begin(), cargo_deliveries.end(),
		                                [&](const auto &job)
		                                {
			                                return job.id == batch.cargo;
		                                }))
			throw std::runtime_error("Cargo batch without commitment");
	for (const auto &mission : away_operations.missions())
		if (mission.supply_job && std::none_of(cargo_deliveries.begin(), cargo_deliveries.end(),
		                                       [&](const auto &job)
		                                       {
			                                       return job.id == mission.supply_job && job.collection;
		                                       }))
			throw std::runtime_error("Supply mission without cargo commitment");
	for (const auto &[id, e] : inventory_rows())
		for (const auto craft : e.get<MaterialInventory>().authorised_craft)
			if (!entities.contains(craft) || !entities.at(craft).has<CraftState>() ||
			    entities.at(craft).get<CraftState>().home != id)
				throw std::runtime_error("Invalid standing craft authorisation");
}

auto Runtime::request_cargo_return(CargoDelivery &job) -> void
{
	if (job.phase == "returning" || job.phase == "complete" || job.phase == "lost")
		return;
	if (job.collection)
	{
		job.cancelled = true;
		job.phase = "cancelling";
		sync_cargo_stock(job);
		std::erase_if(material_requests,
		              [&](const auto &r)
		              {
			              return r.cargo == job.id;
		              });
		for (auto &batch : material_batches)
			if (batch.cargo == job.id)
				return_material_batch(batch, true);
		return;
	}
	job.phase = "returning";
	job.reserved = 0;
	sync_cargo_stock(job);
	std::erase_if(material_requests,
	              [&](const auto &r)
	              {
		              return r.cargo == job.id;
	              });
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
	for (auto &batch : material_batches)
		if (batch.source == job.carrier && batch.destination == job.target)
			return_material_batch(batch, true);
	craft_return(craft);
}
} // namespace phoenix::world_detail
