module;
#include "native_headers.hpp"

module phoenix.simulation;
import :projection_bindings;
import phoenix.world.schedule.profiling;

namespace phoenix::world_detail
{
auto Runtime::append_sensor_contacts(const ConsoleAddress &address, Observation &result,
                                     const std::map<EntityId, Json> &evidence, ContactSnapshot *contact_snapshot) const
    -> void
{
	const auto entity = entities.at(address.ship);
	const auto &own = entity.get<Pose>();
	std::map<EntityId, BodyView> contacts;
	std::map<EntityId, BearingContactView> bearings;
	for (auto &sensor : result.systems)
		if (sensor.kind == OperatingSystem::Radar && sensor.enabled)
		{
			auto append = [&](const BodyView &body, SensorContact detection)
			{
				sensor.contact_ids.push_back(body.id);
				if (detection == SensorContact::Position)
					contacts.emplace(body.id, body);
				else
					bearings.emplace(
					    body.id, BearingContactView{body.id, combat_bearing(fixed::Vec{body.x, body.y} - own.position),
					                                ecs.get<Clock>().tick});
			};
			if (contact_snapshot)
			{
				auto [found, inserted] = contact_snapshot->try_emplace({address.ship, sensor.id});
				if (inserted)
					for_sensor_contacts(
					    address.ship, sensor.id,
					    [&](const auto &input)
					    {
						    found->second.emplace_back(input.body, static_cast<int>(input.detection));
					    },
					    true);
				for (const auto &[body, detection] : found->second)
					append(body, static_cast<SensorContact>(detection));
			}
			else
				for_sensor_contacts(
				    address.ship, sensor.id,
				    [&](const auto &input)
				    {
					    append(input.body, input.detection);
				    },
				    true);
		}
	for (auto &[id, body] : contacts)
		result.contacts.push_back(std::move(body));
	for (const auto &[id, bearing] : bearings)
		if (!contacts.contains(id))
			result.bearing_contacts.push_back(bearing);
	for (auto missile : projectile_rows())
	{
		const auto &p = missile.get<Projectile>();
		if (!p.definition.durability || missile.get<Destructible>().durability <= 0)
			continue;
		bool visible = false, bearing_visible = false;
		const auto contact_id = 7000000000000000ULL + p.id;
		for (auto &sensor : result.systems)
			if (sensor.kind == OperatingSystem::Radar && sensor.enabled &&
			    std::find(sensor.shows.begin(), sensor.shows.end(), "missile") != sensor.shows.end())
			{
				const auto detection = sensor_contact(address.ship, sensor.id, p.position,
				                                      fixed::length(p.position - own.position) +
				                                          std::abs(p.altitude - altitude(address.ship)));
				if (detection == SensorContact::None)
					continue;
				sensor.contact_ids.push_back(contact_id);
				bearing_visible = true;
				visible |= detection == SensorContact::Position;
			}
		if (!visible && bearing_visible)
			result.bearing_contacts.push_back(
			    {contact_id, combat_bearing(p.position - own.position), ecs.get<Clock>().tick});
		if (visible)
		{
			result.contacts.push_back(
			    {contact_id, p.definition.visual, p.position.x, p.position.y, p.velocity.x, p.velocity.y, p.heading});
			result.contacts.back().altitude = p.altitude;
			result.contacts.back().vz = p.vz;
			if (p.source == address.ship)
				result.contacts.back().disposition = "friendly";
		}
	}
	for (auto &contact : result.contacts)
		if (entities.contains(contact.id) || ghost_receptions.contains(contact.id))
		{
			Json identity;
			if (evidence.contains(contact.id) && evidence.at(contact.id).at("findings").contains("basic_identity"))
				identity = evidence.at(contact.id).at("findings").at("basic_identity").at("data");
			const bool identified = !identity.is_null();
			contact.definition = identified ? identity.at("class").get<std::string>() : "Unidentified contact";
			if (!identified)
			{
				contact.radar_sprite.clear();
				contact.faction.clear();
				contact.faction_label = "Unknown allegiance";
				contact.disposition = "unknown";
			}
			else
			{
				const auto &data = identity;
				contact.faction = data.at("faction");
				contact.faction_label = data.at("faction_label");
				contact.disposition = data.at("disposition");
			}
		}
	std::sort(result.bearing_contacts.begin(), result.bearing_contacts.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.id < b.id;
	          });
	std::sort(result.contacts.begin(), result.contacts.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.id < b.id;
	          });
}

auto Runtime::capture_sensor_contacts(const ConsoleAddress &address) const -> std::vector<BodyView>
{
	return capture_sensor_observation(address).contacts;
}

auto Runtime::capture_console_observation(const ConsoleAddress &address, Json gameplay,
                                          TelemetrySnapshot *telemetry_snapshot,
                                          ContactSnapshot *contact_snapshot) const -> Observation
{
	TickWorkProfile profile("projection.console_capture");
	RuleSourceReadScope source_reads(*this);
	const auto entity = entities.at(address.ship);
	const auto &own = entity.get<Pose>();
	const auto &seat = *this->seat(address.ship, address.console);
	Observation result{view(address.ship, entity.get<Definition>(), own, entity.get<Motion>()), {}};
	result.own.faction = (entity.try_get<FactionMembership>() ? entity.get<FactionMembership>().id : std::string{});
	result.own.faction_label = result.own.faction.empty() ? "Unaffiliated" : factions.labels.at(result.own.faction);
	result.own.disposition = factions.relationship(result.own.faction, result.own.faction);
	result.console = seat.id;
	result.regions = region_markers(regions);
	result.observed_tick = ecs.get<Clock>().tick;
	result.controls = seat.controls;
	result.feeds = seat.feeds;
	result.gameplay = std::move(gameplay);
	if (const auto *bridge = entity.try_get<BridgeBlackboard>())
	{
		result.gameplay["objective_priorities"] = bridge->priority_objectives;
		result.gameplay["objective_priority_revision"] = bridge->priority_revision;
		result.gameplay["alert"] = alert_board(address.ship);
		result.gameplay["stance"] = stance_board(address);
	}
	const auto combat = capture_combat_snapshot(address);
	if (combat.present)
		result.gameplay["combat"] = combat_decision_json(combat);
	result.gameplay["ftl"] = ftl_board(address);
	if (const auto craft = capture_craft_self_decision(address, combat))
		result.gameplay["craft_self"] = *craft;
	result.gameplay["power"] = power_board(address);
	result.gameplay["processors"] = project_processors(address);
	result.gameplay["logistics"] = logistics_board(address);
	result.gameplay["docking"] = docking_board(address);
	result.gameplay["directed"] = Json::array();
	for (const auto &id : seat.feed_systems)
		if (directed_definition(address.ship, id))
		{
			auto row = directed_projection(address.ship, id);
			row["id"] = id;
			result.gameplay["directed"].push_back(std::move(row));
		}

	const auto evidence = sensor_evidence(address);
	result.gameplay["sensors"] = sensor_projection(address, evidence);
	result.engine_enabled = false;
	result.radar_enabled = false;
	for (const auto &id : seat.feed_systems)
	{
		const auto instance = this->instance(entity, id);
		SystemView state;
		state.id = id;
		state.controlled = false;
		const auto *engine = instance ? instance->engine : nullptr;
		const auto *turn = instance ? instance->steering : nullptr;
		const auto *radar = instance ? instance->radar : nullptr;
		if (engine)
		{
			state.kind = OperatingSystem::Engine;
			state.requested = engine->speed;
			state.maximum = engine->max_speed;
			state.enabled = engine->enabled;
		}
		else if (turn)
		{
			state.kind = OperatingSystem::Steering;
			state.requested = turn->heading;
			state.maximum = 359999;
		}
		else if (radar)
		{
			state.kind = OperatingSystem::Radar;
			state.maximum = radar->range;
			state.enabled = radar->enabled;
		}
		if (instance && entity.has<NamedEquipment>())
		{
			if (telemetry_snapshot)
			{
				const auto key = std::pair{address.ship, instance->ordinal};
				auto found = telemetry_snapshot->find(key);
				if (found == telemetry_snapshot->end())
					found = telemetry_snapshot->emplace(key, telemetry(address.ship, instance->ordinal)).first;
				state.telemetry = found->second;
			}
			else
				state.telemetry = telemetry(address.ship, instance->ordinal);
		}
		if (instance && instance->advanced)
		{
			const auto index = static_cast<std::size_t>(instance->ordinal);
			state.kind = instance->kind;
			state.enabled = state.telemetry.at("enabled") != 0;
			state.requested = state.telemetry.at("requested");
			if (auto runtime = gameplay_systems(entity); runtime && runtime->values[index].flight)
				state.maximum = state.kind == OperatingSystem::Steering ? 359999
				                : state.kind == OperatingSystem::Lateral
				                    ? runtime->values[index].flight->definition.lateral_speed
				                    : runtime->values[index].flight->definition.maximum_speed;
			else if (state.kind != OperatingSystem::Radar)
				state.maximum = instance->maximum;
		}
		if (state.kind == OperatingSystem::Radar)
		{
			state.shows = definitions.at(entity.get<Definition>().name).radar_shows.at(id);
			state.effective_range = sensor_range(address.ship, id);
			const auto &envelope = sensor_envelope(address.ship, id, state.effective_range);
			state.radial_ranges = envelope.distances;
			state.reach_profile = envelope.profile;
			state.reach_uncertainty_mm = envelope.uncertainty;
			state.reach_limited = envelope.limited;
		}
		if (instance && structurally_disabled(entity, id))
		{
			state.enabled = false;
			state.status = state.telemetry["destroyed"] ? "Destroyed"
			               : state.telemetry["damaged"] ? "Damaged"
			                                            : "Disabled: compartment structure";
		}
		const auto &data = definition(address.ship);
		const auto &t = state.telemetry;
		const bool structural = t.contains("structural_disabled") && t.at("structural_disabled");
		const bool damaged =
		    t.contains("damaged") && t.at("damaged") && (!t.contains("destroyed") || !t.at("destroyed"));
		if (data.combat && (structural || damaged) && data.combat->equipment_compartments.contains(id))
			result.gameplay["repair_needs"].push_back(
			    {{"system", id},
			     {"compartment_index", data.combat->compartment_ids.at(data.combat->equipment_compartments.at(id)) + 1},
			     {"observed_tick", result.observed_tick},
			     {"structural", structural}});
		result.systems.push_back(std::move(state));
	}
	if (entity.has<NamedEquipment>())
	{
		std::int64_t range = 0;
		for (const auto &state : result.systems)
		{
			if (state.kind == OperatingSystem::Engine)
			{
				result.requested_speed = state.requested;
				result.engine_enabled = state.enabled;
			}
			else if (state.kind == OperatingSystem::Steering)
				result.requested_heading = state.requested;
			else if (state.kind == OperatingSystem::Radar && state.enabled)
				range = std::max(range, sensor_range(address.ship, state.id));
		}
		result.radar_enabled = range > 0;
		append_sensor_contacts(address, result, evidence, contact_snapshot);
		return result;
	}
	return result;
}

auto Runtime::capture_decision_observation(const ConsoleAddress &address, DecisionTelemetrySnapshot *telemetry_snapshot,
                                           ContactSnapshot *contact_snapshot) const -> DecisionObservation
{
	TickWorkProfile profile("projection.decision_capture");
	RuleSourceReadScope source_reads(*this);
	const auto entity = entities.at(address.ship);
	const auto &seat = *this->seat(address.ship, address.console);
	DecisionObservation result;
	result.own = view(address.ship, entity.get<Definition>(), entity.get<Pose>(), entity.get<Motion>());
	result.own.faction = entity.try_get<FactionMembership>() ? entity.get<FactionMembership>().id : std::string{};
	result.own.faction_label = result.own.faction.empty() ? "Unaffiliated" : factions.labels.at(result.own.faction);
	result.own.disposition = factions.relationship(result.own.faction, result.own.faction);
	result.console = seat.id;
	result.controls = seat.controls;
	result.feeds = seat.feeds;
	result.observed_tick = ecs.get<Clock>().tick;
	result.authority = capture_decision_authority(address);
	result.combat = capture_combat_snapshot(address);
	result.ftl = capture_ftl_snapshot(address);
	result.craft_self = capture_craft_self_decision(address, result.combat);
	result.logistics.revision = logistics_revision(entity);
	const auto *docking = entity.try_get<DockingState>();
	result.docking.revision = docking ? docking->revision : 1;
	result.engine_enabled = false;
	result.radar_enabled = false;
	for (const auto &id : seat.feed_systems)
	{
		const auto instance = this->instance(entity, id);
		DecisionSystemView state;
		state.id = id;
		state.controlled = false;
		const auto *engine = instance ? instance->engine : nullptr;
		const auto *turn = instance ? instance->steering : nullptr;
		const auto *radar = instance ? instance->radar : nullptr;
		if (engine)
		{
			state.kind = OperatingSystem::Engine;
			state.requested = engine->speed;
			state.maximum = engine->max_speed;
			state.enabled = engine->enabled;
		}
		else if (turn)
		{
			state.kind = OperatingSystem::Steering;
			state.requested = turn->heading;
			state.maximum = 359999;
		}
		else if (radar)
		{
			state.kind = OperatingSystem::Radar;
			state.maximum = radar->range;
			state.enabled = radar->enabled;
		}
		if (instance && entity.has<NamedEquipment>())
		{
			if (telemetry_snapshot)
			{
				const auto key = std::pair{address.ship, instance->ordinal};
				auto found = telemetry_snapshot->find(key);
				if (found == telemetry_snapshot->end())
					found = telemetry_snapshot->emplace(key, decision_telemetry(address.ship, instance->ordinal)).first;
				state.telemetry = found->second;
			}
			else
				state.telemetry = decision_telemetry(address.ship, instance->ordinal);
		}
		if (instance && instance->advanced)
		{
			const auto index = static_cast<std::size_t>(instance->ordinal);
			state.kind = instance->kind;
			state.enabled = state.telemetry.at(TelemetryField::enabled) != 0;
			state.requested = state.telemetry.at(TelemetryField::requested);
			if (auto runtime = gameplay_systems(entity); runtime && runtime->values[index].flight)
				state.maximum = state.kind == OperatingSystem::Steering ? 359999
				                : state.kind == OperatingSystem::Lateral
				                    ? runtime->values[index].flight->definition.lateral_speed
				                    : runtime->values[index].flight->definition.maximum_speed;
			else if (state.kind != OperatingSystem::Radar)
				state.maximum = instance->maximum;
		}
		if (state.kind == OperatingSystem::Radar)
		{
			state.shows = definitions.at(entity.get<Definition>().name).radar_shows.at(id);
			state.effective_range = sensor_range(address.ship, id);
		}
		if (instance && structurally_disabled(entity, id))
		{
			state.enabled = false;
			state.status = state.telemetry[TelemetryField::destroyed] ? "Destroyed"
			               : state.telemetry[TelemetryField::damaged] ? "Damaged"
			                                                          : "Disabled: compartment structure";
		}
		{
			const auto &data = definition(address.ship);
			const auto &t = state.telemetry;
			const bool structural =
			    t.contains(TelemetryField::structural_disabled) && t.at(TelemetryField::structural_disabled);
			const bool damaged = t.contains(TelemetryField::damaged) && t.at(TelemetryField::damaged) &&
			                     (!t.contains(TelemetryField::destroyed) || !t.at(TelemetryField::destroyed));
			if (data.combat && (structural || damaged) && data.combat->equipment_compartments.contains(id))
				result.repair_needs.push_back(
				    {id,
				     static_cast<std::int64_t>(
				         data.combat->compartment_ids.at(data.combat->equipment_compartments.at(id)) + 1),
				     result.observed_tick, structural});
		}
		result.systems.push_back(std::move(state));
	}

	if (const auto *power = entity.try_get<ShipResources>())
	{
		const auto &data = definition(address.ship);
		const auto &network = power->definition.network;
		std::set<ResourceIndex> cooled_stores;
		if (data.layout)
			for (const auto &controller : data.layout->controllers)
				if (controller.console == address.console)
					for (const auto &command : controller.commands)
						if (command.action == OperatingAction::CoolingAllocation)
							for (const auto &binding : power->definition.systems)
								if (binding.cooling != no_resource &&
								    std::find(command.consumer_ids.begin(), command.consumer_ids.end(), binding.id) !=
								        command.consumer_ids.end())
									cooled_stores.insert(network.cooling_links.at(binding.cooling).store);
		for (const auto &binding : power->definition.systems)
			if (binding.generator != no_resource &&
			    cooled_stores.contains(network.generators.at(binding.generator).store) &&
			    std::find(seat.feed_systems.begin(), seat.feed_systems.end(), binding.id) == seat.feed_systems.end())
			{
				const auto store = network.generators.at(binding.generator).store;
				const auto &state = power->network.stores().at(store);
				DecisionSystemView thermal;
				thermal.id = binding.id;
				thermal.kind = OperatingSystem::Generator;
				thermal.telemetry[TelemetryField::heat] = state.heat;
				thermal.telemetry[TelemetryField::heat_capacity] = network.stores.at(store).stop_heat;
				thermal.telemetry[TelemetryField::thermal_stopped] = state.thermal_stop;
				thermal.telemetry[TelemetryField::actual_cooling] =
				    power->network.telemetry().at(store).heat_removed * rate;
				result.systems.push_back(std::move(thermal));
			}
	}

	for (const auto &state : result.systems)
	{
		if (state.kind == OperatingSystem::Engine)
		{
			result.requested_speed = state.requested;
			result.engine_enabled = state.enabled;
		}
		else if (state.kind == OperatingSystem::Steering)
			result.requested_heading = state.requested;
		else if (state.kind == OperatingSystem::Radar && state.enabled && state.effective_range > 0)
			result.radar_enabled = true;
	}
	result.sensors = capture_sensor_snapshot(address);
	append_decision_sensor_contacts(address, result, contact_snapshot);
	if (!entity.has<EntityControls>())
	{
		result.machinery = capture_machinery_decision(address, result);
		result.sensors = {};
		return result;
	}
	const auto &controllers = entity.get<EntityControls>().controllers;
	const bool workflows =
	    native_operation_controller(address, "medical") || native_operation_controller(address, "craft") ||
	    native_operation_controller(address, "contribution") || native_operation_controller(address, "mission") ||
	    std::any_of(controllers.begin(), controllers.end(),
	                [&](const auto &controller)
	                {
		                return controller.definition.console == address.console && controller.brain &&
		                       (controller.brain->report_observations || controller.brain->workflow_base ||
		                        controller.brain->medical_base);
	                });
	if (!workflows)
		result.sensors = {};
	if (workflows)
	{
		result.teams = capture_team_snapshot(address);
		result.away_missions = capture_away_snapshot(address);
		result.craft = capture_craft_snapshot(address);
		result.docking = capture_docking_snapshot(address);
		result.psychic = capture_psychic_snapshot(address);
		result.logistics = capture_logistics_snapshot(address);
		result.authority.alert_maximum =
		    std::max<std::int64_t>(0, definition(address.ship).bridge.conditions.size()) - 1;
	}
	if (native_operation_controller(address, "personnel"))
		result.personnel = capture_personnel_snapshot(address);
	if (std::any_of(controllers.begin(), controllers.end(),
	                [&](const auto &controller)
	                {
		                return controller.definition.console == address.console && controller.brain &&
		                       controller.brain->docking_revision;
	                }))
		result.docking = capture_docking_snapshot(address);
	result.machinery = capture_machinery_decision(address, result);
	return result;
}

auto Runtime::capture_console_contacts(const ConsoleAddress &address) const -> std::vector<BodyView>
{
	return capture_sensor_contacts(address);
}
} // namespace phoenix::world_detail
