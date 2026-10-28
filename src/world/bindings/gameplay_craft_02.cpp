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
auto Runtime::step_vertical() -> void
{
	std::vector<std::pair<EntityId, flecs::entity>> rows;
	vertical_query.each(
	    [&](flecs::entity e, const StableId &id, VerticalMotion &)
	    {
		    rows.emplace_back(id.id, e);
	    });
	std::sort(rows.begin(), rows.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.first < b.first;
	          });
	for (const auto &[id, e] : rows)
	{
		if (craft_dormant(e) || combat_dead(e))
			continue;
		auto &v = e.get_mut<VerticalMotion>();
		const auto &d = *definition(id).craft;
		int effectiveness = 0;
		for (auto child : e.get<EquipmentIndex>().values)
			if (child.get<Equipment>().kind == OperatingSystem::Engine && child.get<Equipment>().enabled &&
			    (!child.has<FlightRuntime>() || child.get<FlightRuntime>().state.enabled) &&
			    !structurally_disabled(e, child.get<Equipment>().id))
			{
				const auto *p = e.try_get<ShipResources>();
				const auto paid = p && child.has<ResourceBinding>()
				                      ? p->grants.at(child.get<ResourceBinding>().slot).supply_permille
				                      : 1000;
				const auto process_paid = native_process_paid(id, "propulsion", child.get<Equipment>().id, paid);
				effectiveness =
				    std::max(effectiveness,
				             static_cast<int>(process_paid * fault_effectiveness(e, child.get<Equipment>().id) / 1000));
			}
		advance_craft_vertical(v, e.get_mut<Pose>(), e.get_mut<Motion>(), d, effectiveness, rate);
	}
}

auto Runtime::craft_service_rate(EntityId home, std::string_view kind, const std::string &native) const -> std::int64_t
{
	const auto driver = finite_native_driver(home, kind, native);
	if (!driver.state)
		return 0;
	const auto &definition = process_definition(*driver.state);
	const auto scale = process_scale(definition.rate, process_expression_context(driver.owner, *driver.state));
	return scale ? *scale : 0;
}

auto Runtime::stage_craft_service_energy(FiniteNativeWork &quote, EntityId home, EntityId craft, ResourceIndex from,
                                         ResourceIndex to, std::int64_t offered, bool stage) -> std::int64_t
{
	auto &source =
	    quote.prepared.payments.try_emplace(home, entities.at(home).get<ShipResources>().network).first->second.network;
	auto &target = quote.prepared.payments.try_emplace(craft, entities.at(craft).get<ShipResources>().network)
	                   .first->second.network;
	return phoenix::stage_craft_service_energy(source, target, quote.prepared.stocks, from, to, offered, stage);
}

auto Runtime::service_craft_energy(EntityId home, const std::string &bay, EntityId craft, ResourceIndex from,
                                   ResourceIndex to, std::int64_t offered) -> std::int64_t
{
	const auto &network = entities.at(craft).get<ShipResources>().network;
	// Native demand precedes admission. A full, protected or disconnected reserve
	// must not create an episode or charge entry costs merely to quote zero work.
	auto source_preview = entities.at(home).get<ShipResources>().network;
	auto target_preview = network;
	offered = source_preview.transfer_external(target_preview, from, to, offered);
	if (!offered)
		return 0;
	const auto progress = network.stores().at(to).energy();
	const auto required = network.definition().stores.at(to).capacity;
	const auto target = "craft:" + std::to_string(craft) + ":energy:" + std::to_string(to);
	for (std::size_t attempt = 0; offered > 0 && attempt < 64; ++attempt)
	{
		auto quote = prepare_finite_native_work(home, "servicing_energy", bay, target, RuleActor{craft}, progress,
		                                        required, offered, {}, false);
		if (!quote)
			return 0;
		const auto accepted = stage_craft_service_energy(*quote, home, craft, from, to, quote->work, false);
		if (accepted < quote->work)
		{
			offered = accepted;
			continue;
		}
		if (stage_craft_service_energy(*quote, home, craft, from, to, accepted, true) != accepted)
			throw std::runtime_error("Servicing energy preview changed before commit");
		if (!commit_finite_native_payment(*quote))
			return 0;
		const auto delivered = entities.at(craft).get<ShipResources>().network.stores().at(to).energy();
		commit_finite_native_work(*quote, accepted, delivered, required, delivered >= required);
		return accepted;
	}
	return 0;
}

auto Runtime::validate_craft_service_attribution(EntityId home, EntityId craft, std::size_t from, std::size_t to,
                                                 const MaterialServiceAttribution &service) const -> void
{
	if (service.bay.empty())
	{
		if (!service.native.empty() || !service.process.empty() || service.generation)
			throw std::runtime_error("Invalid empty servicing attribution");
		return;
	}
	if (!entities.contains(home) || !definition(home).craft_bays.contains(service.bay) || service.process.empty() ||
	    service.process.size() > 1024 || !service.generation || service.generation >= process_generation_limit)
		throw std::runtime_error("Invalid servicing source attribution");
	const auto &bay = definition(home).craft_bays.at(service.bay);
	for (std::size_t i = 0; i < bay.ammunition.size(); ++i)
		if (service.native == service.bay + ".ammo-" + std::to_string(i))
		{
			if (from != bay.ammunition[i].source_store)
				break;
			if (!craft)
				return; // A cancelled batch may already be returning to its source.
			if (!entities.contains(craft) || !entities.at(craft).has<CraftState>())
				break;
			const auto &state = entities.at(craft).get<CraftState>();
			const auto found = bay.ammunition[i].destination_stores.find(entities.at(craft).get<Definition>().name);
			if (state.home == home && state.bay == service.bay && found != bay.ammunition[i].destination_stores.end() &&
			    found->second == to)
				return;
			break;
		}
	throw std::runtime_error("Invalid servicing destination attribution");
}

auto Runtime::prepare_craft_service_delivery(const MaterialBatch &batch, std::optional<FiniteNativeWork> &work) -> bool
{
	if (batch.service.bay.empty() || batch.returning)
		return true;
	const auto address = custom_property_address(RuleActor{batch.source, batch.service.bay});
	const auto *state = process_state(address, batch.service.process);
	// Dispatch already committed the finite supplies to physical logistics. A
	// withdrawn grant leaves delivery independent, without crediting a replacement.
	if (!state || state->generation != batch.service.generation || !state->requested)
		return true;
	const auto progress = material_quantity(batch.destination, batch.to, batch.material);
	const auto required = material_store(batch.destination, batch.to).capacity /
	                      material_size(batch.destination, batch.to, batch.material);
	const auto target =
	    "craft:" + std::to_string(batch.destination) + ":ammunition:" + std::to_string(batch.to) + ":" + batch.material;
	work = prepare_finite_native_work(batch.source, "servicing_ammunition", batch.service.native, target,
	                                  RuleActor{batch.destination}, progress, required, batch.quantity, {}, false);
	if (!work)
		return false;
	auto &stock = material_stock(batch.destination, batch.to);
	const auto claim = material_batch_claim(batch.id);
	if (work->work == batch.quantity)
	{
		if (!work->prepared.stocks.receive_reserved(stock, claim))
			throw std::runtime_error("Missing servicing delivery receiving commitment");
	}
	else
	{
		// A proportionally funded delivery consumes only its accepted portion;
		// the same physical batch retains the exact remaining receiving claim.
		const auto remaining = batch.quantity - work->work;
		if (remaining <= 0 || !work->prepared.stocks.release_capacity(stock, claim) ||
		    !work->prepared.stocks.add(stock, batch.material, work->work) ||
		    !work->prepared.stocks.reserve_capacity(stock, claim, batch.material, remaining))
			throw std::runtime_error("Missing partial servicing receiving commitment");
	}
	return commit_finite_native_payment(*work);
}

auto Runtime::service_craft() -> void
{
	const auto rows = craft_rows();
	for (const auto &[id, e] : rows)
		if (craft_dormant(e) && e.get<CraftState>().phase != CraftPhase::Lost &&
		    e.get<CraftState>().phase != CraftPhase::Consumed)
			if (auto *p = e.try_get_mut<ShipResources>())
				p->network.dormant_tick();
	std::set<std::pair<EntityId, std::string>> visited;
	for (const auto &[id, e] : rows)
	{
		const auto &s = e.get<CraftState>();
		if (!visited.emplace(s.home, s.bay).second || !bay_working(s.home, s.bay))
			continue;
		const auto &d = definition(s.home).craft_bays.at(s.bay);
		auto &bs = away_equipment(s.home, s.bay).get_mut<CraftBayState>();
		bs.energy_remainder += d.charge_rate * craft_service_rate(s.home, "servicing_energy", s.bay) / 1000;
		auto energy = bs.energy_remainder / rate;
		bs.energy_remainder %= rate;
		std::vector<std::int64_t> rounds;
		for (std::size_t i = 0; i < d.ammunition.size(); ++i)
		{
			const auto native = s.bay + ".ammo-" + std::to_string(i);
			bs.ammunition_remainders[i] +=
			    d.ammunition[i].rate * craft_service_rate(s.home, "servicing_ammunition", native) / 1000;
			rounds.push_back(bs.ammunition_remainders[i] / rate);
			bs.ammunition_remainders[i] %= rate;
		}
		for (const auto &[other, craft] : rows)
		{
			const auto &c = craft.get<CraftState>();
			if (c.home != s.home || c.bay != s.bay || c.phase != CraftPhase::Docked)
				continue;
			if (auto *source = entities.at(s.home).try_get_mut<ShipResources>();
			    source && energy && !d.power_store.empty())
				if (auto *target = craft.try_get_mut<ShipResources>())
					for (ResourceIndex store = 0; store < target->network.stores().size(); ++store)
						energy -=
						    service_craft_energy(s.home, s.bay, other,
						                         source->definition.network.store_ids.at(d.power_store), store, energy);
			for (std::size_t i = 0; i < d.ammunition.size(); ++i)
			{
				const auto &connection = d.ammunition[i];
				const auto name = craft.get<Definition>().name;
				const auto bound = connection.destination_stores.find(name);
				if (bound == connection.destination_stores.end())
					continue;
				const auto to = bound->second;
				const auto &source_stock = material_stock(s.home, connection.source_store);
				const auto &resource = source_stock.selected();
				const auto &destination_stock = material_stock(other, to);
				if (resource.empty() || !source_stock.accepts(resource) || !destination_stock.accepts(resource))
					continue;
				const auto amount =
				    std::min(rounds[i], std::max<std::int64_t>(0, material_store(other, to).capacity /
				                                                          material_size(other, to, resource) -
				                                                      material_pending(other, to, resource)));
				if (amount > 0)
				{
					const auto native = s.bay + ".ammo-" + std::to_string(i);
					const auto driver = finite_native_driver(s.home, "servicing_ammunition", native);
					if (!driver.state)
						continue;
					const auto request = next_material_request;
					request_material(s.home, connection.source_store, other, to, resource, amount, false);
					if (next_material_request != request)
					{
						material_requests.back().service = {s.bay, native, driver.state->id, driver.state->generation};
						rounds[i] -= amount;
					}
				}
			}
		}
	}
}

auto Runtime::update_craft_reports() -> void
{
	for (const auto &[id, e] : craft_rows())
	{
		if (!craft_link(id))
			continue;
		auto &s = e.get_mut<CraftState>();
		CraftReport r;
		r.phase = s.phase;
		r.order = s.order;
		r.target = s.target;
		r.tick = ecs.get<Clock>().tick;
		if (const auto *p = e.try_get<ShipResources>())
			for (std::size_t i = 0; i < p->network.stores().size(); ++i)
			{
				r.energy += p->network.stores()[i].energy();
				r.capacity += p->definition.network.stores[i].capacity;
			}
		if (const auto *c = e.try_get<CombatStructure>())
			for (std::size_t i = 0; i < c->compartments.size(); ++i)
			{
				r.condition += c->compartments[i].hp;
				r.maximum_condition += c->definition.compartments[i].maximum;
			}
		for (auto child : e.get<EquipmentIndex>().values)
			if (const auto *m = child.try_get<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Magazine)
			{
				const auto &stock = physical_stock(e, m->get<MagazineState>().stock);
				r.ammunition += stock.quantity(stock.selected());
			}
		s.report = r;
	}
}

auto Runtime::craft_board(const ConsoleAddress &a) const -> Json
{
	Json out = Json::array();
	for (const auto &[id, e] : craft_rows())
	{
		const auto &s = e.get<CraftState>();
		if (s.home != a.ship || enabled_system_owner(a.ship, s.bay) != a)
			continue;
		const auto &r = s.report;
		const auto blocker = !bay_working(s.home, s.bay) ? "Shuttle bay unavailable"
		                     : ftl_preparing(s.home)     ? "FTL preparation: launches suspended"
		                                                 : "";
		out.push_back({{"id", std::to_string(id)},
		               {"bay", s.bay},
		               {"role", definition(id).craft->role},
		               {"revision", s.revision},
		               {"phase", r.phase},
		               {"order", r.order},
		               {"target", r.target},
		               {"linked", craft_link(id)},
		               {"pending", !s.pending_order.empty()},
		               {"available", craft_available(id)},
		               {"report_tick", r.tick},
		               {"energy", r.energy},
		               {"capacity", r.capacity},
		               {"ammunition", r.ammunition},
		               {"condition", r.condition},
		               {"maximum_condition", r.maximum_condition}});
		out.back()["blocker"] = blocker;
		bool manual = false;
		for (const auto &controller : entities.at(a.ship).get<EntityControls>().controllers)
			if (controller.definition.console == a.console)
				for (const auto &command : controller.definition.commands)
					for (const auto consumer : command.consumers)
						if (definition(a.ship).fitted->values.at(consumer).id == s.bay)
							manual |= controller.human || assisted_workload(a, "craft");
		out.back()["manual"] = manual && manual_workload(a, "craft");
	}
	return out;
}

auto Runtime::save_craft() const -> Json
{
	Json out{{"schema", 2}, {"entities", Json::array()}, {"bays", Json::array()}};
	std::set<std::pair<EntityId, std::string>> bays;
	for (const auto &[id, e] : craft_rows())
	{
		const auto &s = e.get<CraftState>();
		out["entities"].push_back({{"id", id}, {"state", s}});
		bays.emplace(s.home, s.bay);
	}
	for (const auto &[home, bay] : bays)
		out["bays"].push_back(
		    {{"home", home}, {"bay", bay}, {"state", away_equipment(home, bay).get<CraftBayState>()}});
	return out;
}

auto Runtime::restore_craft(const Json &saved) -> void
{
	const auto initial = save_craft();
	if (saved.is_null() && initial.at("entities").empty())
		return;
	if (!saved.is_object() || (saved.at("schema") != 1 && saved.at("schema") != 2) ||
	    !saved.at("entities").is_array() || saved.at("entities").size() != initial.at("entities").size() ||
	    !saved.at("bays").is_array() || saved.at("bays").size() != initial.at("bays").size())
		throw std::runtime_error("Invalid saved craft roster");
	std::set<EntityId> seen;
	const std::set<std::string> phases{"docked", "launching", "flying", "recovering", "lost", "consumed"};
	const std::set<std::string> orders{"idle", "hold", "return", "defend", "escort", "bomb", "transport", "pickup"};
	for (const auto &row : saved.at("entities"))
	{
		const auto id = row.at("id").get<EntityId>();
		if (!entities.contains(id) || !entities.at(id).has<CraftState>() || !seen.insert(id).second ||
		    !phases.contains(row.at("state").at("phase").get<std::string>()) ||
		    !phases.contains(row.at("state").at("report").at("phase").get<std::string>()))
			throw std::runtime_error("Invalid saved craft identity/phase");
		auto e = entities.at(id);
		const auto s = row.at("state").get<CraftState>();
		const auto &expected = e.get<CraftState>();
		const auto &bay = definition(expected.home).craft_bays.at(expected.bay);
		const auto v = e.get<VerticalMotion>();
		for (const auto &point : {s.rendezvous, s.rendezvous_velocity, s.target_position, s.target_velocity,
		                          s.pending_position, s.pending_velocity})
			if (std::abs(point.x) > fixed::unit || std::abs(point.y) > fixed::unit)
				throw std::runtime_error("Invalid saved craft navigation sample");
		if (std::abs(s.rendezvous_altitude) > 1000000 || std::abs(s.target_altitude) > 1000000 ||
		    std::abs(s.pending_altitude) > 1000000 || (s.target && !entities.contains(s.target)) ||
		    (s.pending_target && !entities.contains(s.pending_target)))
			throw std::runtime_error("Invalid saved craft target");
		if (s.home != expected.home || s.bay != expected.bay || s.slot != expected.slot || !s.revision ||
		    s.revision > 9007199254740991ULL || s.progress < 0 || s.progress >= bay.transfer_ticks ||
		    !orders.contains(s.order) || (!s.pending_order.empty() && !orders.contains(s.pending_order)) ||
		    s.mission.size() > 64 || s.report.tick > ecs.get<Clock>().tick || s.report.energy < 0 ||
		    s.report.capacity < s.report.energy || s.report.ammunition < 0 || s.report.condition < 0 ||
		    s.report.maximum_condition < s.report.condition || std::abs(v.altitude) > 1000000 ||
		    std::abs(v.desired) > 1000000 || std::abs(v.velocity) > 1000000 || std::abs(v.remainder) >= rate ||
		    std::abs(v.acceleration_remainder) >= rate)
			throw std::runtime_error("Invalid saved craft continuation");
		if (saved.at("schema") == 1 && (s.phase == CraftPhase::Consumed || s.report.phase == CraftPhase::Consumed))
			throw std::runtime_error("Consumed craft requires continuation schema 2");
		if ((s.phase == CraftPhase::Consumed &&
		     (definition(id).craft->role != "boarding_torpedo" || !s.mission.empty() || s.order != "idle" ||
		      !s.pending_order.empty() || s.pending_target || s.progress)) ||
		    (definition(id).craft->role == "boarding_torpedo" &&
		     (s.phase == CraftPhase::Recovering || s.order == "return" || s.order == "pickup" ||
		      (!s.pending_order.empty() &&
		       (s.pending_order != "return" || (s.phase != CraftPhase::Docked && s.phase != CraftPhase::Launching) ||
		        s.pending_target)))))
			throw std::runtime_error("Invalid saved torpedo continuation");
		if (!s.mission.empty() &&
		    std::none_of(away_operations.definitions().begin(), away_operations.definitions().end(),
		                 [&](const auto &d)
		                 {
			                 return d.id == s.mission && away_ship(d) == s.home;
		                 }))
			throw std::runtime_error("Invalid saved craft commitment");
		e.set<CraftState>(s).set<VerticalMotion>(v);
		e.get_mut<Pose>().altitude = v.altitude;
		e.get_mut<Motion>().vertical_velocity = v.velocity;
		set_craft_active(e, !craft_dormant(e));
	}
	std::set<std::pair<EntityId, std::string>> bays;
	for (const auto &row : saved.at("bays"))
	{
		const auto home = row.at("home").get<EntityId>();
		const auto bay = row.at("bay").get<std::string>();
		if (!entities.contains(home) || !definition(home).craft_bays.contains(bay) || !bays.emplace(home, bay).second)
			throw std::runtime_error("Invalid saved bay identity");
		const auto state = row.at("state").get<CraftBayState>();
		if (state.energy_remainder < 0 || state.energy_remainder >= rate ||
		    state.ammunition_remainders.size() != definition(home).craft_bays.at(bay).ammunition.size() ||
		    std::any_of(
		        state.ammunition_remainders.begin(), state.ammunition_remainders.end(),
		        [](auto v)
		        {
			        return v < 0 || v >= rate;
		        }))
			throw std::runtime_error("Invalid saved servicing continuation");
		away_equipment(home, bay).set<CraftBayState>(state);
	}
}

auto Runtime::validate_craft_transports() const -> void
{
	std::set<EntityId> reserved;
	for (std::size_t i = 0; i < away_operations.missions().size(); ++i)
	{
		const auto &m = away_operations.missions()[i];
		const auto &d = away_operations.definitions()[i];
		const auto home = away_ship(d);
		const auto id = home ? away_transport_entity(home, m.transport) : 0;
		if (m.aboard_craft &&
		    (!id || id == home ||
		     (m.phase != AwayPhase::Outbound && m.phase != AwayPhase::Inbound && m.phase != AwayPhase::Lost)))
			throw std::runtime_error("Invalid saved passenger location");
		if (m.transport_released && (!id || id == home || m.aboard_craft || m.phase != AwayPhase::Deployed))
			throw std::runtime_error("Invalid saved shuttle release");
		if (id && id != home &&
		    (m.phase == AwayPhase::Outbound || m.phase == AwayPhase::Inbound ||
		     (m.phase == AwayPhase::Deployed && !m.transport_released)))
		{
			const auto &s = entities.at(id).get<CraftState>();
			if ((s.mission != d.id && !(m.supply_job && m.phase == AwayPhase::Inbound &&
			                            s.phase == CraftPhase::Docked && s.mission.empty())) ||
			    (s.phase == CraftPhase::Docked && !m.supply_job && m.escorts.empty() &&
			     (m.aboard_craft || s.pending_order != "return")) ||
			    s.phase == CraftPhase::Lost || s.phase == CraftPhase::Consumed)
				throw std::runtime_error("Saved shuttle commitment mismatch");
		}
		if (id && id != home && !m.transport_released && m.phase != AwayPhase::Lost &&
		    !(m.phase == AwayPhase::Recovered && !m.outcome.empty()) && !reserved.insert(id).second)
			throw std::runtime_error("Duplicate saved mission transport");
		for (const auto &[slot, escort] : m.escorts)
			if (!reserved.insert(escort).second)
				throw std::runtime_error("Duplicate saved mission escort");
	}
	for (const auto &[id, e] : craft_rows())
	{
		const auto &state = e.get<CraftState>();
		if (state.mission.empty())
			continue;
		bool committed = false;
		for (std::size_t i = 0; i < away_operations.missions().size(); ++i)
		{
			const auto &mission = away_operations.missions()[i];
			committed |= away_operations.definitions()[i].id == state.mission &&
			             away_ship(away_operations.definitions()[i]) == state.home &&
			             (mission.transport == "craft:" + std::to_string(id) ||
			              std::any_of(mission.escorts.begin(), mission.escorts.end(),
			                          [&](const auto &s)
			                          {
				                          return s.second == id;
			                          })) &&
			             (!mission.transport_released || std::any_of(mission.escorts.begin(), mission.escorts.end(),
			                                                         [&](const auto &s)
			                                                         {
				                                                         return s.second == id;
			                                                         })) &&
			             (mission.phase == AwayPhase::Outbound || mission.phase == AwayPhase::Inbound ||
			              mission.phase == AwayPhase::Deployed || mission.phase == AwayPhase::Recovered ||
			              mission.phase == AwayPhase::Lost);
		}
		if (!committed)
			throw std::runtime_error("Orphaned saved craft commitment");
	}
}
} // namespace phoenix::world_detail
