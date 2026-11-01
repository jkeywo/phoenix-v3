module;
#include "native_headers.hpp"

module phoenix.simulation;
import :projection_bindings;

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

auto Runtime::capture_sensor_observation(const ConsoleAddress &address) const -> Observation
{
	Observation result;
	const auto entity = entities.at(address.ship);
	for (const auto &id : seat(address.ship, address.console)->feed_systems)
		if (const auto fit = instance(entity, id); fit && fit->radar)
		{
			SystemView sensor;
			sensor.kind = OperatingSystem::Radar;
			sensor.id = id;
			sensor.effective_range = sensor_range(address.ship, id);
			sensor.enabled = sensor.effective_range > 0;
			sensor.shows = definitions.at(entity.get<Definition>().name).radar_shows.at(id);
			result.systems.push_back(std::move(sensor));
		}
	append_sensor_contacts(address, result, sensor_evidence(address));
	return result;
}

auto Runtime::capture_sensor_contacts(const ConsoleAddress &address) const -> std::vector<BodyView>
{
	return capture_sensor_observation(address).contacts;
}

auto Runtime::capture_console_observation(const ConsoleAddress &address, Json gameplay, ObservationDetail detail,
                                          TelemetrySnapshot *telemetry_snapshot,
                                          ContactSnapshot *contact_snapshot) const -> Observation
{
	const auto entity = entities.at(address.ship);
	const auto &own = entity.get<Pose>();
	const auto &seat = *this->seat(address.ship, address.console);
	const bool presentation = detail == ObservationDetail::Presentation;
	Observation result{view(address.ship, entity.get<Definition>(), own, entity.get<Motion>()), {}};
	result.own.faction = (entity.try_get<FactionMembership>() ? entity.get<FactionMembership>().id : std::string{});
	result.own.faction_label = result.own.faction.empty() ? "Unaffiliated" : factions.labels.at(result.own.faction);
	result.own.disposition = factions.relationship(result.own.faction, result.own.faction);
	result.console = seat.id;
	if (presentation)
		result.regions = region_markers(regions);
	result.observed_tick = ecs.get<Clock>().tick;
	result.controls = seat.controls;
	result.feeds = seat.feeds;
	result.gameplay = std::move(gameplay);
	if (detail != ObservationDetail::Contacts)
	{
		if (const auto *bridge = entity.try_get<BridgeBlackboard>())
		{
			result.gameplay["objective_priorities"] = bridge->priority_objectives;
			result.gameplay["objective_priority_revision"] = bridge->priority_revision;
			result.gameplay["alert"] = alert_board(address.ship);
			result.gameplay["stance"] = stance_board(address);
		}
		project_combat(address, result);
		result.gameplay["ftl"] = ftl_board(address);
		if (const auto *craft = entity.try_get<CraftState>())
		{
			std::int64_t energy = 0, capacity = 0;
			if (const auto *resources = entity.try_get<ShipResources>())
				for (std::size_t i = 0; i < resources->network.stores().size(); ++i)
				{
					energy += resources->network.stores()[i].energy();
					capacity += resources->definition.network.stores[i].capacity;
				}
			bool armed = false;
			// Utility craft can have no combat structure and therefore no weapon projection.
			if (const auto combat = result.gameplay.find("combat"); combat != result.gameplay.end())
				for (const auto &weapon : combat->at("weapons"))
					if (weapon.value("enabled", false) && weapon.value("durable_payload", false))
						for (const auto &option : weapon.at("ammunition_options"))
							armed |= option.at("available").get<bool>();
			const auto anchor = craft->target == craft->home ? craft->rendezvous : craft->target_position;
			auto &row = result.gameplay["craft_self"] = {
			    {"order", craft->order},
			    {"target", craft->target},
			    {"reserve_low",
			     capacity > 0 && energy * 1000 <= capacity * definition(address.ship).craft->return_reserve},
			    {"armed", armed},
			    {"anchor_x", anchor.x},
			    {"anchor_y", anchor.y},
			    {"engagement_range", definition(address.ship).craft->engagement_range},
			    {"pursuit_altitude_offset",
			     definition(address.ship).craft->cruise_altitude + craft_flight_band(address.ship)}};
			if (const auto *g = entity.try_get<CraftGuidance>())
				row["guidance"] = {{"x", g->destination.x}, {"y", g->destination.y}, {"altitude", g->altitude},
				                   {"speed", g->speed},     {"target", g->target},   {"navigate", g->navigate},
				                   {"heading", g->heading}};
		}
	}
	if (presentation)
	{
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
	}
	else if (detail == ObservationDetail::Decision)
	{
		result.gameplay["logistics"] = {{"revision", logistics_revision(entity)}};
		const auto *docking = entity.try_get<DockingState>();
		result.gameplay["docking"] = {{"revision", docking ? docking->revision : 1}};
	}
	const auto evidence = sensor_evidence(address);
	if (presentation)
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
			if (presentation)
			{
				const auto &envelope = sensor_envelope(address.ship, id, state.effective_range);
				state.radial_ranges = envelope.distances;
				state.reach_profile = envelope.profile;
				state.reach_uncertainty_mm = envelope.uncertainty;
				state.reach_limited = envelope.limited;
			}
		}
		if (instance && structurally_disabled(entity, id))
		{
			state.enabled = false;
			state.status = state.telemetry["destroyed"] ? "Destroyed"
			               : state.telemetry["damaged"] ? "Damaged"
			                                            : "Disabled: compartment structure";
		}
		if (detail != ObservationDetail::Contacts)
		{
			const auto &data = definition(address.ship);
			const auto &t = state.telemetry;
			const bool structural = t.contains("structural_disabled") && t.at("structural_disabled");
			const bool damaged =
			    t.contains("damaged") && t.at("damaged") && (!t.contains("destroyed") || !t.at("destroyed"));
			if (data.combat && (structural || damaged) && data.combat->equipment_compartments.contains(id))
				result.gameplay["repair_needs"].push_back(
				    {{"system", id},
				     {"compartment_index",
				      data.combat->compartment_ids.at(data.combat->equipment_compartments.at(id)) + 1},
				     {"observed_tick", result.observed_tick},
				     {"structural", structural}});
		}
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

auto Runtime::capture_decision_observation(const ConsoleAddress &address, TelemetrySnapshot *telemetry_snapshot,
                                           ContactSnapshot *contact_snapshot) const -> Observation
{
	auto result = capture_console_observation(address, Json::object(), ObservationDetail::Decision, telemetry_snapshot,
	                                          contact_snapshot);
	result.gameplay["ability_facts"] = ability_decision_facts(address, result);
	result.gameplay["processes"] = process_board(address, &result);
	result.gameplay["process_facts"] = process_decision_facts(result);
	const auto owner = entities.find(address.ship);
	if (owner == entities.end() || !owner->second.has<EntityControls>())
		return result;
	const auto &controllers = owner->second.get<EntityControls>().controllers;
	if (native_operation_controller(address, "medical") || native_operation_controller(address, "craft") ||
	    native_operation_controller(address, "contribution") || native_operation_controller(address, "mission") ||
	    std::any_of(controllers.begin(), controllers.end(),
	                [&](const auto &controller)
	                {
		                return controller.definition.console == address.console && controller.brain &&
		                       (controller.brain->report_observations || controller.brain->workflow_base ||
		                        controller.brain->medical_base);
	                }))
	{
		// Reuse the same scope-checked producers as the human projection.
		result.gameplay["teams"] = team_board(address);
		result.gameplay["away_missions"] = away_board(address);
		result.gameplay["craft"] = craft_board(address);
		result.gameplay["ftl"] = ftl_board(address);
		result.gameplay["docking"] = docking_board(address);
		result.gameplay["psychic"] = psychic_board(address);
		result.gameplay["logistics"] = logistics_board(address);
		result.gameplay["sensors"] = sensor_projection(address, sensor_evidence(address));
		result.gameplay["alert_maximum"] =
		    std::max<std::int64_t>(0, definition(address.ship).bridge.conditions.size()) - 1;
	}
	if (native_operation_controller(address, "personnel"))
		result.gameplay["personnel"] = personnel_board(address);

	if (std::any_of(controllers.begin(), controllers.end(),
	                [&](const auto &controller)
	                {
		                return controller.definition.console == address.console && controller.brain &&
		                       controller.brain->docking_revision;
	                }))
		result.gameplay["docking"] = docking_board(address);
	return result;
}

auto Runtime::capture_console_contacts(const ConsoleAddress &address) const -> std::vector<BodyView>
{
	return capture_console_observation(address, Json::object(), ObservationDetail::Contacts).contacts;
}
} // namespace phoenix::world_detail
