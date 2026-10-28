module;
#include "native_headers.hpp"

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::operate(flecs::entity entity, const std::string &name, OperatingRequest output, bool execute) -> bool
{
	if (output.action == OperatingAction::DirectedOperatingMode ||
	    output.action == OperatingAction::DirectedTargetingMode || output.action == OperatingAction::DirectedTarget)
		return output.arguments.empty() &&
		       directed_setting(entity.get<StableId>().id, name,
		                        output.action == OperatingAction::DirectedOperatingMode   ? "operating_mode"
		                        : output.action == OperatingAction::DirectedTargetingMode ? "targeting_mode"
		                                                                                  : "target",
		                        output.value, execute);
	if (output.action == OperatingAction::Altitude)
	{
		const auto equipment = instance(entity, name);
		if (!entity.has<VerticalMotion>() || !equipment || equipment->kind != OperatingSystem::Engine ||
		    !output.arguments.empty() || output.value < -1000000 || output.value > 1000000)
			return false;
		if (execute)
			entity.get_mut<VerticalMotion>().desired = output.value;
		return true;
	}
	if (output.action == OperatingAction::SensorTarget)
		return output.arguments.empty() && sensor_target(entity.get<StableId>().id, name, output.value, execute);
	if (auto target = instance(entity, name);
	    target && (target->kind == OperatingSystem::Comms || target->kind == OperatingSystem::Displacement ||
	               target->kind == OperatingSystem::Dispatch || target->kind == OperatingSystem::Ftl ||
	               target->kind == OperatingSystem::ShuttleBay || target->kind == OperatingSystem::Psychic))
	{
		if (output.action != OperatingAction::SystemEnabled || output.value < 0 || output.value > 1 ||
		    !output.arguments.empty())
			return false;
		if (execute)
			target->enabled = output.value != 0;
		return true;
	}
	if (entity.has<CombatStructure>())
		if (auto target = instance(entity, name); target && target->entity.has<CombatMechanism>())
			return operate_combat(entity, target->entity, output, execute);
	auto fitted = fittings(entity);
	auto systems = gameplay_systems(entity);
	if (!fitted)
		return false;
	if (!systems)
	{
		const auto target = instance(entity, name);
		if (!target || !output.arguments.empty())
			return false;
		if (output.action == OperatingAction::SystemEnabled)
		{
			if (target->kind == OperatingSystem::Engine)
				output.action = OperatingAction::EngineEnabled;
			else if (target->kind == OperatingSystem::Radar)
				output.action = OperatingAction::RadarEnabled;
			else
				return false;
		}
		if (!admissible(output, target->engine, target->steering, target->radar))
			return false;
		if (execute)
		{
			apply(output, target->engine, target->steering, target->radar);
			if (target->engine)
				target->enabled = target->engine->enabled;
			if (target->radar)
				target->enabled = target->radar->enabled;
		}
		return true;
	}
	std::size_t index = 0;
	for (; index < fitted->values.size(); ++index)
		if (fitted->values[index].id == name)
			break;
	if (index == fitted->values.size() || !operating_value_valid(operating_action(output.action), output.value) ||
	    !operating_arguments_valid(output.action, output.value, output.arguments))
		return false;
	if (output.action == OperatingAction::FollowContact && output.value &&
	    static_cast<EntityId>(output.arguments[0]) == entity.get<StableId>().id)
		return false;
	auto system = systems->values[index];
	if (system.flight)
		if (auto kind = flight_action(output.action))
		{
			const auto &definition = system.flight->definition;
			if (*kind == FlightCommandKind::Impulse && output.value &&
			    system.flight->state.impulse == ImpulsePhase::Idle && definition.impulse_minimum_reserve)
			{
				const auto *resources = entity.try_get<ShipResources>();
				if (!resources)
					return false;
				const auto store = resources->definition.systems[index].store;
				if (store == no_resource || !resources->network.stores()[store].enabled ||
				    resources->network.stores()[store].thermal_stop ||
				    resources->network.stores()[store].available() < definition.impulse_minimum_reserve)
					return false;
			}
			auto candidate = system.flight->state;
			const auto result =
			    apply_flight(system.flight->definition, candidate, {*kind, output.value, output.arguments},
			                 entity.get<Pose>(), entity.get<Motion>());
			if (result.accepted && execute)
			{
				system.flight->state = std::move(candidate);
				if (output.action == OperatingAction::SystemEnabled || output.action == OperatingAction::EngineEnabled)
					fitted->values[index].enabled = system.flight->state.enabled;
			}
			return result.accepted;
		}
	if (fitted->values[index].radar &&
	    (output.action == OperatingAction::RadarEnabled || output.action == OperatingAction::SystemEnabled))
	{
		if (execute)
		{
			fitted->values[index].radar->enabled = output.value != 0;
			fitted->values[index].enabled = output.value != 0;
		}
		return output.value == 0 || output.value == 1;
	}
	if (output.action == OperatingAction::SystemEnabled && fitted->values[index].kind == OperatingSystem::Coolant)
	{
		if (output.value != 0 && output.value != 1)
			return false;
		if (execute)
		{
			fitted->values[index].enabled = output.value != 0;
			if (systems->power)
				control_resource(*systems->power, fitted->values[index].entity.get<ResourceBinding>().slot,
				                 ResourceControl::CoolingIsolated, output.value ? 0 : 1);
		}
		return true;
	}
	if (systems->power)
		if (auto kind = resource_action(output.action))
		{
			const auto accepted =
			    execute
			        ? control_resource(*systems->power, fitted->values[index].entity.get<ResourceBinding>().slot, *kind,
			                           output.value)
			        : can_control_resource(*systems->power, fitted->values[index].entity.get<ResourceBinding>().slot,
			                               *kind, output.value);
			if (accepted && execute && output.action == OperatingAction::SystemEnabled)
				fitted->values[index].enabled = output.value != 0;
			return accepted;
		}
	if (output.action == OperatingAction::RadarEnabled && fitted->values[index].radar)
	{
		if (execute)
			fitted->values[index].radar->enabled = output.value != 0;
		return true;
	}
	return false;
}

auto Runtime::publish_waypoint(EntityId ship, const ControllerChannelState &state, bool reached) -> void
{
	const auto &schema = knowledge.definition();
	const auto key = schema.schema("phoenix.waypoint");
	KnowledgeRecordInput record;
	record.schema = key;
	record.producer = schema.role("phoenix.scenario");
	record.subject = ship;
	record.provenance = {ship, ecs.get<Clock>().tick, state.revision * 2 + (reached ? 1 : 0), 0, EvidenceKind::Fact};
	record.fields = {{schema.field(key, "active"), state.value != 0},
	                 {schema.field(key, "reached"), reached},
	                 {schema.field(key, "x"), state.arguments.at(0)},
	                 {schema.field(key, "y"), state.arguments.at(1)},
	                 {schema.field(key, "revision"), static_cast<std::int64_t>(state.revision)}};
	knowledge.publish_ship(ship, record, ecs.get<Clock>().tick);
}

auto Runtime::waypoint_arrivals() -> void
{
	for (const auto &[id, entity] : entities)
		if (entity.has<EntityControls>())
			for (const auto &controller : entity.get<EntityControls>().controllers)
				for (std::size_t i = 0; i < controller.commands.size(); ++i)
				{
					const auto &state = controller.commands[i];

					if (controller.definition.commands[i].action != OperatingAction::Waypoint || !state.value)
						continue;
					const auto board = knowledge.project_json({id, controller.definition.console});
					bool reached = false;
					for (const auto &record : board.at("shared"))
						if (record.at("schema") == "phoenix.waypoint")
							reached = record.at("fields").value("reached", false);
					const auto from = step_starts.at(id), to = entity.get<Pose>().position;
					ScenarioRegion region;
					region.centre = {state.arguments[0], state.arguments[1]};
					region.radius = controller.definition.commands[i].arrival_radius;
					if (!reached && scenario_crosses(region, {from.x, from.y}, {to.x, to.y}))
						publish_waypoint(id, state, true);
				}
}

auto Runtime::sensor_range(EntityId ship, const std::string &name, bool outer) const -> std::int64_t
{
	const auto entity = entities.at(ship);
	const auto sensor = instance(entity, name);
	if (!sensor || !sensor->radar || !sensor->enabled || !sensor->radar->enabled || structurally_disabled(entity, name))
		return 0;
	auto range =
	    outer ? std::max(sensor->radar->range, sensor_definition(ship, name).long_range) : sensor->radar->range;
	if (sensor->kind == OperatingSystem::Weapon && directed_definition(ship, name))
	{
		const auto *mode = directed_mode(ship, name);
		if (!mode || mode->operation != DirectedOperationKind::Scan)
			return 0;
		range = mode->range;
	}
	range = effective_property(entity, name, "sensors", range);
	if (auto systems = gameplay_systems(entity); systems && systems->power)
	{
		const auto index = sensor->entity.get<ResourceBinding>().slot;
		const auto &power = *systems->power;
		const auto &binding = power.definition.systems[index];
		if (binding.store != no_resource)
		{
			const auto &store = power.network.stores()[binding.store];
			if (!store.enabled || (store.thermal_stop && !store.thermal_override))
				return 0;
		}
		range = range * power.grants[index].supply_permille / 1000;
	}
	return range;
}

auto Runtime::sensor_envelope(EntityId ship, const std::string &name, std::int64_t range) const
    -> const SensorEnvelope &
{
	return sensor_envelopes.get(ship, name, entities.at(ship).get<Pose>().position, range, regions);
}

auto Runtime::telemetry(EntityId id, std::size_t index) const -> std::map<std::string, std::int64_t>
{
	auto entity = entities.at(id);
	const auto fitted = (*fittings(entity)).values[index];
	std::map<std::string, std::int64_t> values{{"enabled", fitted.enabled}, {"requested", 0}};
	if (fitted.engine)
	{
		values["requested"] = fitted.engine->speed;
		values["enabled"] = fitted.engine->enabled;
	}
	if (fitted.steering)
		values["requested"] = fitted.steering->heading;
	if (fitted.radar)
		values["enabled"] = fitted.radar->enabled;
	if (auto systems = gameplay_systems(entity))
	{
		const auto system = systems->values[index];
		if (system.flight)
		{
			const auto &f = system.flight->state;
			values["requested"] =
			    system.flight->definition.kind == FlightKind::Steering ? f.desired_heading : f.desired_speed;
			values["desired_speed"] = f.desired_speed;
			values["desired_heading"] = f.desired_heading;
			values["enabled"] = f.enabled;
			values["minimum_speed"] = -system.flight->definition.reverse_speed;
			values["assisted"] = f.assisted;
			values["longitudinal_thrust"] = f.longitudinal;
			values["lateral_thrust"] = f.lateral;
			values["manual_steering"] = f.steering;
			values["boost"] = f.boost;
			values["impulse"] = f.impulse != ImpulsePhase::Idle;
			values["impulse_phase"] = static_cast<unsigned>(f.impulse);
			values["impulse_progress"] = f.charge_milliticks;
			values["impulse_charge_spent"] = f.charge_spent;
			values["impulse_charge_total"] = system.flight->definition.impulse_charge_ticks * 1000;
			values["impulse_reason"] = f.impulse_reason;
			values["effective_boost"] = f.effective_boost;
			values["lateral_direction"] = system.flight->definition.lateral_direction;
			values["lateral_acceleration"] = system.flight->definition.lateral_acceleration;
			values["lateral_maximum_speed"] = system.flight->definition.lateral_speed;
			values["impulse_ready"] = 0;
			if (const auto *resources = entity.try_get<ShipResources>())
			{
				const auto store = resources->definition.systems[index].store;
				values["impulse_ready"] =
				    f.enabled && store != no_resource && resources->network.stores()[store].enabled &&
				    !resources->network.stores()[store].thermal_stop &&
				    resources->network.stores()[store].available() >= system.flight->definition.impulse_minimum_reserve;
			}
			values["match_speed"] = f.matching;
			values["selected_contact"] = f.selected_contact;
			values["matched_contact"] = f.matched_contact;
			values["match_stale"] = f.match_stale;
			values["navigating"] = f.navigating;
			values["destination_x"] = f.destination.x;
			values["destination_y"] = f.destination.y;
			values["following"] = f.following;
			values["follow_stale"] = f.follow_stale;
			values["follow_distance"] = f.follow_distance;
			values["follow_speed_limit"] = f.follow_speed_limit;
			values["matched_speed"] = f.matched_speed;
			values["actual_longitudinal"] = f.actual_longitudinal;
			values["actual_lateral"] = f.actual_lateral;
			values["actual_steering"] = f.actual_steering;
		}
		if (systems->power)
		{
			const auto &p = *systems->power;
			const auto &binding = p.definition.systems[fitted.entity.get<ResourceBinding>().slot];
			const auto &g = p.grants[fitted.entity.get<ResourceBinding>().slot];
			values["supply_permille"] = g.supply_permille;
			values["used_power"] = (g.paid + g.discrete) * rate;
			values["reserved_power"] = g.requested * rate;
			values["supply_limited"] = g.supply_permille < 1000;
			if (binding.store != no_resource)
			{
				const auto &d = p.definition.network.stores[binding.store];
				const auto &v = p.network.stores()[binding.store];
				values["enabled"] =
				    values.at("enabled") && v.enabled && (!system.flight || system.flight->state.enabled);
				const auto &t = p.network.telemetry()[binding.store];
				values["stored_energy"] = v.energy();
				values["energy_capacity"] = d.capacity;
				values["incoming_power"] = t.accepted_incoming * rate;
				values["heat"] = v.heat;
				values["heat_capacity"] = d.stop_heat;
				values["stress"] = v.stress;
				values["thermal_stopped"] = v.thermal_stop;
				values["thermal_override"] = v.thermal_override;
				values["overdrive"] = v.overdrive;
				values["charge_limit"] = v.charge_limit;
				values["discharge_limit"] = v.discharge_limit;
				values["passive_cooling"] = d.passive_cooling;
				values["actual_cooling"] = t.heat_removed * rate;
				const auto forecast = p.network.forecast(binding.store);
				values["depletion_ticks"] = forecast.depletion_ticks;
				values["thermal_stop_ticks"] = forecast.thermal_stop_ticks;
				values["full_ticks"] = forecast.full_ticks;
				values["thermal_recovery_ticks"] = forecast.thermal_recovery_ticks;
			}
			if (binding.generator != no_resource)
			{
				const auto &v = p.network.generators()[binding.generator];
				values["requested"] = v.commanded_rate;
				values["generator_output"] = v.commanded_rate;
				values["actual_output"] = v.actual_rate;
				const auto &generator = p.definition.network.generators[binding.generator];
				const auto &load = p.network.telemetry()[generator.store];
				// Previous committed load, including exported power and storage losses.
				values["demand_output"] =
				    std::min(generator.maximum_rate, (load.outgoing + load.consumed + load.lost) * rate);
			}
			if (binding.group != no_resource)
			{
				const auto &v = p.network.groups()[binding.group];
				values["requested"] = v.budget_rate;
				values["group_allocation"] = v.budget_rate;
				values["flow_cut"] = v.cut;
			}
			if (binding.conduit != no_resource)
				values["link_isolated"] = p.network.links()[binding.conduit].isolated;
			if (binding.cooling != no_resource)
			{
				const auto &v = p.network.cooling_links()[binding.cooling];
				values["requested"] = v.allocation_rate;
				values["cooling_allocation"] = v.allocation_rate;
				const auto source = p.definition.network.cooling_links[binding.cooling].source;
				const auto forecast = p.network.coolant_forecast(source);
				values["coolant_depletion_ticks"] = forecast.depletion_ticks;
				values["actual_coolant_used"] = forecast.observed_coolant_used * rate;
				values["coolant_stock"] =
				    p.network.coolant_sources()[p.definition.network.cooling_links[binding.cooling].source].remaining();
			}
		}
	}
	combat_telemetry(entity, fitted.entity, values);
	return values;
}

auto Runtime::native_flight_ports() -> NativeFlightPorts
{
	NativeFlightPorts ports;
	ports.property =
	    [this](EntityId actor, const std::string &equipment, PropertyKey key, std::int64_t base, std::int64_t extra)
	{
		return effective_property(entities.at(actor), equipment, key, base, extra);
	};
	ports.structurally_disabled = [this](EntityId actor, const std::string &equipment)
	{
		return structurally_disabled(entities.at(actor), equipment);
	};
	ports.docked = [this](EntityId actor)
	{
		return dock_attached(actor);
	};
	ports.propulsion_rate = [this](EntityId actor, const std::string &equipment)
	{
		const auto view = native_process_view(actor, "propulsion", equipment);
		return view.state ? view.state->rate_permille : 0;
	};
	ports.contacts = [this](EntityId actor, const std::string &equipment) -> std::span<const BodyView>
	{
		return decision_frame.observations().at(system_owner(actor, equipment)).contacts;
	};
	ports.telemetry = [this](EntityId actor, const std::string &equipment, std::size_t ordinal)
	{
		knowledge.ingest_telemetry(actor, equipment, telemetry(actor, ordinal), ecs.get<Clock>().tick);
	};
	ports.tick = [this]
	{
		return ecs.get<Clock>().tick;
	};
	return ports;
}

auto Runtime::request_passive_resources(flecs::entity ship) -> void
{
	const auto id = ship.get<StableId>().id;
	for (auto child : ordered_equipment(passive_resource_query, id))
	{
		const auto &fit = child.get<Equipment>();
		const auto radar = child.try_get<Radar>();
		request_system_resource(
		    ship.get_mut<ShipResources>(), child.get<ResourceBinding>().slot,
		    fit.enabled && !structurally_disabled(ship, fit.id) && (!radar || radar->enabled) &&
		            fit.kind != OperatingSystem::Psychic &&
		            (fit.kind != OperatingSystem::Coolant ||
		             (child.get<ResourceBinding>().slot < ship.get<ShipResources>().definition.systems.size() &&
		              native_process_rate(id, "cooling",
		                                  ship.get<ShipResources>()
		                                      .network.definition()
		                                      .cooling_links
		                                      .at(ship.get<ShipResources>()
		                                              .definition.systems[child.get<ResourceBinding>().slot]
		                                              .cooling)
		                                      .id))) &&
		            ((fit.kind != OperatingSystem::Displacement &&
		              !(fit.kind == OperatingSystem::ShuttleBay &&
		                definition(id).craft_bays.at(fit.id).legacy_transport)) ||
		             away_transport_requested(id, fit.id))
		        ? 1000
		        : 0);
	}
}

auto Runtime::quote_process_native_work(const std::vector<std::pair<EntityId, flecs::entity>> &ships,
                                        const std::map<EntityId, std::optional<ShipResourceAllocation>> &allocations,
                                        bool stage) -> bool
{
	std::vector<flecs::entity> flight_actors;
	flight_actors.reserve(ships.size());
	for (const auto &actor : ships)
		flight_actors.push_back(actor.second);
	auto snapshots = flight_native->snapshot(flight_actors);
	try
	{
		for (const auto &[id, ship] : ships)
		{
			const auto found = allocations.find(id);
			flight_native->prepare_paid_modifiers(
			    process_machinery, ship, flight_native->flight_restrictions(process_machinery, ship),
			    found != allocations.end() && found->second ? &*found->second : nullptr);
		}
		const auto changed = capture_process_native_work(allocations, stage);
		flight_native->restore(snapshots);
		return changed;
	}
	catch (...)
	{
		flight_native->restore(snapshots);
		throw;
	}
}

auto Runtime::run_advanced_equipment() -> void
{
	for (const auto &[id, ship] : entities)
		if (ship.has<EntityControls>())
			consume_process_commands(id);
	prepare_processes();
	begin_process_machinery_quotes();
	std::vector<std::pair<EntityId, flecs::entity>> ships;
	advanced_ships.each(
	    [&](flecs::entity entity, const StableId &id, const GameplayShip &)
	    {
		    ships.emplace_back(id.id, entity);
	    });
	std::sort(ships.begin(), ships.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.first < b.first;
	          });
	for (const auto &[id, ship] : ships)
	{
		if (auto *power = ship.try_get_mut<ShipResources>())
			clear_named_requests(*power);
		prepare_abilities(id);
		const auto restrictions = flight_native->flight_restrictions(process_machinery, ship);
		prepare_combat(ship);
		prepare_processors(ship);
		flight_native->request_flight_resources(process_machinery, ship, restrictions);
		request_passive_resources(ship);
		request_combat_resources(ship);
		request_directed_resources(ship);
		request_ability_resources(id);
		if (!definition(id).ftl.empty())
			prepare_ftl(ship);
		flight_native->reserve_paid_modifiers(process_machinery, ship, restrictions);
		request_processor_resources(ship);
	}
	std::map<EntityId, std::optional<ShipResourceAllocation>> allocations;
	capture_process_native_requests();
	const auto allocate_native = [&](bool producers)
	{
		allocations.clear();
		for (const auto &[id, ship] : ships)
			if (auto *power = ship.try_get_mut<ShipResources>())
			{
				std::vector<std::size_t> unavailable;
				for (auto child : ship.get<EquipmentIndex>().values)
					if (structurally_disabled(ship, child.get<Equipment>().id) ||
					    boarding_inhibited(id, child.get<Equipment>().id))
						unavailable.push_back(child.get<ResourceBinding>().slot);
				allocations[id].emplace(*power, unavailable, producers ? process_producer_seed(id) : nullptr);
			}
	};
	prepare_process_machinery_controls();
	allocate_native(false);
	quote_process_native_work(ships, allocations, true);
	std::vector<RuleProcessWork> process_work;
	for (std::size_t attempt = 0;; ++attempt)
	{
		if (attempt > 4096)
			throw std::runtime_error("Native process allocation did not settle within supported bounds");
		prepare_process_machinery_controls();
		if (prepare_process_producer_funding())
			continue;
		if (cap_process_native_work(true))
		{
			price_process_native_requests();
			continue;
		}
		request_rule_upkeep();
		allocate_native(true);
		bool removed = false;
		for (const auto &[id, ship] : ships)
		{
			auto &allocation = allocations[id];
			auto *power = ship.try_get_mut<ShipResources>();
			if (!allocation)
				continue;
			for (auto child : ship.get<EquipmentIndex>().values)
			{
				const auto &equipment = child.get<Equipment>();
				if (!child.has<DirectedRuntime>() ||
				    !directed_operation(id, equipment.id, DirectedOperationKind::Launch))
					continue;
				const auto slot = child.get<ResourceBinding>().slot;
				const auto &grant = allocation->grants().at(slot);
				if (power->requests.at(slot).discrete &&
				    (grant.blocked || grant.supply_permille < 1000 ||
				     (power->definition.systems.at(slot).discrete_energy && !grant.discrete_paid)))
				{
					child.get_mut<CombatMechanism>().get<WeaponState>().ready = false;
					auto &state = child.get_mut<DirectedRuntime>();
					state.ready = false;
					state.blocker = "Insufficient operating power";
					auto &request = power->requests.at(slot);
					request.discrete = false;
					request.discrete_amount = 0;
					request.extra_rate -= directed_mode(id, equipment.id)->energy_per_second;
					auto &operation = directed_operations.at({id, equipment.id});
					if (operation.stock_claim)
					{
						const auto magazine = instance(ship, operation.magazine)->entity;
						auto &stock = physical_stock(ship, magazine.get<CombatMechanism>().get<MagazineState>().stock);
						stock.release_stock("directed-launch:" + equipment.id);
						operation.stock_claim = false;
					}
					removed = true;
				}
			}
		}
		if (removed)
		{
			// A withdrawn launch releases real stock. Retry before any quote
			// restaging can continue this loop and lose the withdrawal edge.
			for (const auto &[id, ship] : ships)
				retry_blocked_processors(ship);
			reset_surviving_process_native_caps();
			price_process_native_requests();
		}
		removed = prepare_rule_upkeep_payments(allocations, removed) || removed;
		if (cap_process_native_work())
		{
			price_process_native_requests();
			continue;
		}
		if (quote_process_native_work(ships, allocations, true))
			continue;
		process_work.clear();
		for (const auto &quote : process_machinery.rows())
			process_work.push_back({quote.owner, quote.frozen.id, quote.work, quote.activity});
		if (prepare_process_funding_effects(process_work, allocations))
		{
			cap_process_native_work();
			price_process_native_requests();
			continue;
		}
		if (!removed)
			break;
	}
	for (const auto &[id, ship] : ships)
	{
		auto &allocation = allocations[id];
		flight_native->prepare_paid_modifiers(process_machinery, ship,
		                                      flight_native->flight_restrictions(process_machinery, ship),
		                                      allocation ? &*allocation : nullptr);
	}
	// Freeze this paid operating frame before its work effects change modifiers.
	if (!commit_rule_upkeep_stocks())
		throw std::logic_error("rules: prepared upkeep changed before commit");
	for (auto &[id, allocation] : allocations)
		if (allocation)
			allocation->commit();
	finish_rule_upkeep();
	flight_native->run_basic_steering(process_machinery);
	flight_native->run_basic_engines(process_machinery);
	for (const auto &[id, ship] : ships)
	{
		const auto restrictions = flight_native->flight_restrictions(process_machinery, ship);
		actuate_processors(ship);
		actuate_combat(ship);
		if (!definition(id).ftl.empty())
			actuate_ftl(ship);
		flight_native->run_equipment_actuation(process_machinery, ship, restrictions);
	}
	refresh_ability_work();
	sync_psychic_contributions();
	for (const auto &quote : process_machinery.rows())
		commit_process_work_receipt(quote.owner, quote.frozen, quote.work, quote.activity);
	publish_process_work();
	finalise_ftl_processes();
}

auto Runtime::scenario_effects() -> void
{
	if (!scenario)
		return;
	for (const auto &request : scenario->pending_effects())
	{
		std::string failure;
		try
		{
			if (request.kind == ScenarioEffectKind::Group)
				failure = execute_scenario_effect_group(request);
			else
				apply_scenario_effect(request);
		}
		catch (const std::exception &error)
		{
			failure = error.what();
		}
		scenario->confirm_effect(request.token, std::move(failure));
	}
}
} // namespace phoenix::world_detail
