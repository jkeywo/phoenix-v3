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
auto Runtime::inventory_rows() const -> std::vector<std::pair<EntityId, flecs::entity>>
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

auto Runtime::material_store(EntityId id, std::size_t store) const -> const MaterialStoreDefinition &
{
	return definition(id).logistics.stores.at(store);
}

auto Runtime::material_accepts(EntityId id, std::size_t store, const std::string &m) const -> bool
{
	const auto &s = material_store(id, store);
	return std::find(s.accepts.begin(), s.accepts.end(), m) != s.accepts.end();
}

auto Runtime::physical_stock(flecs::entity ship, const std::string &key) const -> StockStore &
{
	return ship.get_mut<ShipStockStores>().stores.at(key);
}

auto Runtime::select_physical_resource(flecs::entity ship, const std::string &key, const std::string &resource,
                                       bool execute) -> bool
{
	const auto selection = select_material_resource(physical_stock(ship, key), ship.try_get_mut<MaterialInventory>(),
	                                                ship.get<StableId>().id, material_requests, key, resource, execute);
	if (selection != MaterialSelection::Changed)
		return selection == MaterialSelection::Unchanged;
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
			{
				state.loading = 0;
				if (child.has<DirectedRuntime>())
					child.get_mut<DirectedRuntime>().loading_mode.clear();
			}
		}
	return true;
}

auto Runtime::material_stock(EntityId id, std::size_t store) const -> StockStore &
{
	return physical_stock(entities.at(id), entities.at(id).get<MaterialInventory>().stores.at(store).stock);
}

auto Runtime::material_size(EntityId id, std::size_t store, const std::string &m) const -> std::int64_t
{
	return material_stock(id, store).size(m);
}

auto Runtime::material_quantity(EntityId id, std::size_t store, const std::string &m) const -> std::int64_t
{
	return material_stock(id, store).quantity(m);
}

auto Runtime::material_add(EntityId id, std::size_t store, const std::string &m, std::int64_t n) -> void
{
	auto &stock = material_stock(id, store);
	if (!(n < 0 ? stock.consume(m, -n) : stock.add(m, n)))
		throw std::runtime_error("Invalid material stock transaction");
}

auto Runtime::material_occupied_volume(EntityId id, std::size_t store) const -> std::int64_t
{
	const auto &stock = material_stock(id, store);
	return stock.occupied() + stock.reserved_capacity();
}

auto Runtime::material_space(EntityId id, std::size_t store, const std::string &m) const -> std::int64_t
{
	return material_accepts(id, store, m) ? material_stock(id, store).headroom(m) : 0;
}

auto Runtime::material_pending(EntityId id, std::size_t store, const std::string &material) const -> std::int64_t
{
	return pending_material(material_stock(id, store), id, store, material, material_requests, material_batches,
	                        cargo_deliveries);
}

auto Runtime::return_material_batch(MaterialBatch &batch, bool execute) -> bool
{
	// Resolve physical stores only after the original return/location guards.
	if (batch.returning)
		return true;
	if (batch.location != batch.source || batch.crossing)
		return false;
	return phoenix::return_material_batch(batch,
	                                      {[this](EntityId id, std::size_t store) -> StockStore &
	                                       {
		                                       return material_stock(id, store);
	                                       }},
	                                      material_accepts(batch.source, batch.from, batch.material), execute);
}

auto Runtime::material_compartment(EntityId id, std::size_t store) const -> std::size_t
{
	const auto &s = material_store(id, store);
	if (!s.system.empty())
		if (const auto fit = instance(entities.at(id), s.system); fit && fit->kind == OperatingSystem::Team)
			return fit->entity.get<CombatMechanism>().get<TeamState>().location;
	return definition(id).combat->compartment_ids.at(s.compartment);
}

auto Runtime::material_store_working(EntityId id, std::size_t store) const -> bool
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

auto Runtime::material_route(EntityId id, std::size_t from, std::size_t to) const
    -> std::pair<std::int64_t, std::int64_t>
{
	const auto &s = entities.at(id).get<CombatStructure>();
	return physical_route(id, s.compartments.size(), s.definition.connections, from, to, nullptr, true);
}

auto Runtime::material_connection(EntityId a, EntityId b) const -> bool
{
	return material_gateway(a, b).has_value();
}

auto Runtime::request_material(EntityId source, std::size_t from, EntityId destination, std::size_t to,
                               const std::string &m, std::int64_t n, bool automatic) -> void
{
	if (material_requests.size() >= 4096 || next_material_request >= 9007199254740991ULL)
		return;
	material_requests.push_back({next_material_request++, source, destination, from, to, m, n, automatic});
}

auto Runtime::step_logistics() -> void
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
		material_batches.back().service = r.service;
		++active[r.source];
	}
	std::erase_if(material_requests,
	              [](const auto &r)
	              {
		              return r.remaining == 0;
	              });

	MaterialCrossingBudgets crossing_budgets;
	MaterialTransitPorts transit;
	transit.unavailable = [&](EntityId id)
	{
		return combat_dead(entities.at(id)) || departed(id);
	};
	transit.gateway = [&](EntityId source, EntityId destination, bool finishing)
	{
		return material_gateway(source, destination, finishing);
	};
	transit.edge_open = [&](EntityId location, std::size_t from, std::size_t to)
	{
		const auto &structure = entities.at(location).get<CombatStructure>();
		return physical_edge_open(location, from, to) && !structure.compartments.at(from).disabled &&
		       !structure.compartments.at(to).disabled;
	};
	transit.route = [&](EntityId location, std::size_t from, std::size_t to)
	{
		return material_route(location, from, to);
	};
	transit.store_compartment = [&](EntityId id, std::size_t store)
	{
		return material_compartment(id, store);
	};
	for (auto &b : material_batches)
	{
		if (!advance_material_transit(b, transit, crossing_budgets, rate))
			continue;
		if (!material_store_working(b.destination, b.to))
			continue;
		std::optional<FiniteNativeWork> service_work;
		if (!prepare_craft_service_delivery(b, service_work))
			continue;
		if (!service_work && !material_stock(b.destination, b.to).receive_reserved(material_batch_claim(b.id)))
			throw std::runtime_error("Missing delivery receiving commitment");
		if (service_work)
		{
			const auto delivered = material_quantity(b.destination, b.to, b.material);
			const auto required =
			    material_store(b.destination, b.to).capacity / material_size(b.destination, b.to, b.material);
			commit_finite_native_work(*service_work, service_work->work, delivered, required, delivered >= required);
		}
		record_cargo_delivery(cargo_deliveries, b);
		b.quantity -= service_work ? service_work->work : b.quantity;
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

auto Runtime::material_authority(const ConsoleAddress &a, const OperatingCommand &c, const std::string &owner,
                                 bool ai) const -> bool
{
	if (!entities.at(a.ship).has<EntityControls>())
		return false;
	for (const auto &ctrl : entities.at(a.ship).get<EntityControls>().controllers)
		if (ctrl.definition.console == a.console && ctrl.definition.id == owner && c.controller == owner &&
		    ctrl.generation == c.generation && ctrl.human != ai)
			return true;
	return false;
}

auto Runtime::logistics_command(const ConsoleAddress &a, const OperatingCommand &c, bool execute, bool ai) -> bool
{
	const auto e = entities.at(a.ship);
	if (!ai && !manual_workload(a, "logistics") &&
	    !(c.action == "logistics.craft_allow" && cargo_authorisation_console(a)))
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

auto Runtime::parse_material_number(const std::string &s) const -> std::int64_t
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

auto Runtime::logistics_board(const ConsoleAddress &a) const -> Json
{
	Json out{{"revision", 1},
	         {"stores", Json::array()},
	         {"requests", Json::array()},
	         {"batches", Json::array()},
	         {"cargo", Json::array()},
	         {"cargo_craft", Json::array()},
	         {"cargo_authorisation_controller", ""},
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
	out["cargo_authorisation_controller"] = d.cargo_authorisation_controller;
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
			if (main || enabled_system_owner(a.ship, bay) == a || cargo_authorisation_console(a))
			{
				Json stores = Json::array();
				for (const auto &store : definition(id).logistics.stores)
					stores.push_back({{"id", store.id}, {"accepts", store.accepts}});
				out["cargo_craft"].push_back(
				    {{"id", id},
				     {"authorised", state.authorised_craft.contains(id)},
				     {"controllable", d.cargo_authorisation_controller.empty()
				                          ? manual_workload(a, "logistics") && enabled_system_owner(a.ship, bay) == a
				                          : cargo_authorisation_console(a, true)},
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
} // namespace phoenix::world_detail
