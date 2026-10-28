module;
#include "native_headers.hpp"

module phoenix.simulation;
import :persistence;

namespace phoenix
{
using namespace world_detail;
Json World::save() const
{
	if (!impl_->meta_pending.empty() || !impl_->controller_pending.empty())
		throw std::runtime_error("Save at a tick boundary after admitted commands execute");
	Json result{{"schema", 32},           {"tick", ticks()},       {"map", impl_->map},
	            {"ships", impl_->ships},  {"packs", impl_->packs}, {"claims", impl_->ownership.claims()},
	            {"bodies", Json::array()}};
	result["custom_properties"] = impl_->save_custom_properties();
	result["rules"] = impl_->save_rules();
	result["active_effects"] = impl_->save_active_modifiers();
	result["active_effects"]["conditions"] = impl_->active_rule_snapshot();
	result["active_effects"]["next_condition"] = impl_->active_rules.next_identity();
	if (impl_->display_revision)
	{
		result["display_revision"] = impl_->display_revision;
		result["displays"] = Json::array();
		for (const auto &[ship, d] : impl_->displays)
			result["displays"].push_back({{"claimant", d.claimant},
			                              {"revision", d.revision},
			                              {"address", d.address},
			                              {"widget", d.widget},
			                              {"camera", d.camera}});
	}
	if (!impl_->field_rocks.empty())
		result["asteroid_fields"] = impl_->save_fields();
	if (!impl_->regions.empty())
		result["environment_tick"] = impl_->environment_tick;
	for (const auto &[id, entity] : impl_->entities)
	{
		if (id >= Impl::field_identity_base)
			continue;
		SavedBody body{entity.get<Pose>(), entity.get<Motion>(), id, entity.get<Definition>().name};
		result["bodies"].push_back({{"body", body}, {"body_altitude", entity.get<Pose>().altitude}});
		if (entity.has<VerticalMotion>())
			result["bodies"].back()["vertical"] = entity.get<VerticalMotion>();
		if (entity.has<MissionDeparture>())
			result["bodies"].back()["mission_report"] = entity.get<MissionDeparture>().report;
		result["bodies"].back()["faction"] =
		    (entity.try_get<FactionMembership>() ? entity.get<FactionMembership>().id : std::string{});
		if (auto systems = fittings(entity))
		{
			auto &saved = result["bodies"].back()["systems"] = Json::object();
			for (const auto &system : systems->values)
			{
				SavedOperation operation;
				operation.heading = entity.get<Pose>().heading;
				if (system.engine)
				{
					operation.speed = system.engine->speed;
					operation.engine_enabled = system.engine->enabled;
					operation.acceleration_remainder = system.engine->remainder;
					operation.damping_remainder = system.engine->damping_remainder;
				}
				if (system.steering)
				{
					operation.heading = system.steering->heading;
					operation.turn_remainder = system.steering->remainder;
				}
				if (system.radar)
					operation.radar_enabled = system.radar->enabled;
				if (system.advanced)
				{
					operation = SavedOperation{};
					operation.heading = entity.get<Pose>().heading;
				}
				saved[system.id] = operation;
			}
		}
		if (auto systems = gameplay_systems(entity))
		{
			auto &data = result["bodies"].back()["gameplay"] = Json::array();
			for (std::size_t i = 0; i < systems->values.size(); ++i)
			{
				const auto system = systems->values[i];
				Json value{{"id", (*fittings(entity)).values[i].id},
				           {"heat_remainder", system.heat_remainder},
				           {"enabled", (*fittings(entity)).values[i].enabled}};
				if (const auto radar = (*fittings(entity)).values[i].radar)
					value["radar_enabled"] = radar->enabled;
				if (system.flight)
					value["flight"] = save_flight(system.flight->definition, system.flight->state);
				if (const auto *ftl = entity.get<EquipmentIndex>().values.at(i).try_get<FtlReserve>())
					value["ftl_discharge_remainder"] = ftl->discharge_remainder;
				data.push_back(std::move(value));
			}
			if (systems->power)
				result["bodies"].back()["resources"] = save_ship_resources(*systems->power);
		}
		if (entity.has<DockingState>())
			result["bodies"].back()["docking"] = entity.get<DockingState>();
		if (entity.has<DockConnection>())
			result["bodies"].back()["dock_connection"] = entity.get<DockConnection>();
		if (entity.has<ShipProcessors>())
			result["bodies"].back()["processors"] = impl_->save_processors(entity);
		if (entity.has<MaterialInventory>())
			result["bodies"].back()["inventory"] = entity.get<MaterialInventory>();
		if (const auto *stocks = entity.try_get<ShipStockStores>())
		{
			auto &saved_stocks = result["bodies"].back()["stocks"] = Json::object();
			for (const auto &[key, stock] : stocks->stores)
				saved_stocks[key] = stock.save();
		}
		if (entity.has<PersonnelState>())
			result["bodies"].back()["personnel"] = impl_->personnel_snapshot(id);
		if (entity.has<CombatStructure>())
			result["bodies"].back()["combat"] = impl_->save_combat(entity);
		if (entity.has<ShieldComponent>())
			result["bodies"].back()["shields"] = impl_->save_shields(entity);
		if (const auto *d = entity.try_get<Destructible>())
			result["bodies"].back()["destructible"] = *d;
		if (entity.has<EntityControls>())
			result["bodies"].back()["console_controllers"] = save_controllers(entity.get<EntityControls>());
		if (const auto *bridge = entity.try_get<BridgeBlackboard>())
			result["bodies"].back()["bridge"] = {{"condition", bridge->condition},
			                                     {"revision", bridge->revision},
			                                     {"stances", bridge->stances},
			                                     {"epochs", bridge->epochs},
			                                     {"navigation", impl_->navigation_board(id)},
			                                     {"priority_objective", bridge->priority_objective},
			                                     {"priority_revision", bridge->priority_revision}};
	}
	if (impl_->gameplay)
	{
		result["knowledge"] = impl_->knowledge.save();

		result["away"] = Json::array();
		for (const auto &[a, v] : impl_->ownership.away())
			result["away"].push_back({{"address", a}, {"value", v}});
		result["next_entity"] = impl_->next_entity;
		if (impl_->scenario)
			result["scenario"] = impl_->scenario->save();
	}
	result["conversations"] = impl_->conversations.save();
	result["away_missions"] = impl_->save_away();
	result["captured_by"] = impl_->captured_by;
	result["personnel_postings"] = impl_->personnel_postings;
	result["interdictions"] = impl_->save_interdictions();
	result["departure_groups"] = impl_->departures.save_groups();
	result["logistics"] = {{"requests", impl_->material_requests},
	                       {"batches", impl_->material_batches},
	                       {"next_request", impl_->next_material_request},
	                       {"next_batch", impl_->next_material_batch},
	                       {"cargo", impl_->cargo_deliveries},
	                       {"next_cargo", impl_->next_cargo_delivery}};
	result["craft"] = impl_->save_craft();
	result["mission_ledger"] = impl_->mission_ledger;
	result["sensors"] = impl_->save_sensors();
	result["directed"] = impl_->save_directed();
	result["psychic"] = impl_->save_psychic();
	result["abilities"] = impl_->save_abilities();
	result["processes"] = impl_->save_processes();
	result["sensor_discoveries"] = impl_->sensor_findings.discoveries();
	result["conversation_starts"] = impl_->save_conversation_starts();
	result["projectiles"] = impl_->save_projectiles();
	result["blast_visuals"] = impl_->blast_visuals;
	result["next_blast"] = impl_->next_blast;
	result["next_projectile"] = impl_->projectiles.next_identity();
	return result;
}

void World::restore(const Catalog &content, const Json &source_saved)
{
	const Json &saved = source_saved;
	validate_checkpoint_envelope(saved, content.ordered_packs);
	World candidate;
	candidate.load_impl(content, saved.at("map"), saved.at("ships").get<std::map<std::string, std::string>>(), true);
	for (auto &[id, rock] : candidate.impl_->field_rocks)
		candidate.impl_->unload_rock(rock);
	candidate.impl_->field_rocks.clear();
	candidate.impl_->field_window.clear();
	candidate.impl_->index_contacts.run();
	if (candidate.impl_->scenario)
	{
		const auto event_source = candidate.impl_->conversation_instance;
		if (saved.at("conversation_starts").at("instance") != event_source)
			throw std::runtime_error("Saved scenario event source mismatch");
		candidate.impl_->scenario->restore(content, saved.at("map"), saved.at("scenario"), event_source,
		                                   candidate.impl_->property_catalogue);
		for (const auto &[id, definition] : candidate.impl_->scenario->runtime().spawned_entities())
			if (candidate.impl_->entities.contains(id))
				throw std::runtime_error("Spawned identity overlaps initial entity");
			else
				candidate.impl_->create(id, definition, {{"x_mm", 0}, {"y_mm", 0}, {"heading_mdeg", 0}});
		candidate.impl_->restore_spawned_craft(saved.value("craft", Json()));
	}
	candidate.impl_->restore_interdiction_ships(saved.value("interdictions", Json::array()), saved.at("bodies"));
	candidate.impl_->next_entity = checkpoint_next_entity(
	    saved, candidate.impl_->entities.empty() ? EntityId{0} : candidate.impl_->entities.rbegin()->first);
	candidate.impl_->refresh_boarding_templates();
	candidate.impl_->captured_by = saved.value("captured_by", std::map<EntityId, EntityId>{});
	std::vector<EntityId> checkpoint_entities;
	checkpoint_entities.reserve(candidate.impl_->entities.size());
	for (const auto &[id, entity] : candidate.impl_->entities)
		checkpoint_entities.push_back(id);
	CheckpointRoster checkpoint_roster(saved.at("bodies"), checkpoint_entities);
	candidate.impl_->restore_directed(saved.at("directed"));
	for (const auto &value : saved.at("bodies"))
	{
		validate_checkpoint_entity_authority(value);
		auto restored = value.at("body").get<SavedBody>();
		checkpoint_roster.accept(restored.id);
		auto entity = candidate.impl_->entities.at(restored.id);
		if (candidate.impl_->captured_by.contains(restored.id))
		{
			const auto owner = candidate.impl_->captured_by.at(restored.id);
			if (!candidate.impl_->entities.contains(owner) || owner == restored.id)
				throw std::runtime_error("Invalid capture owner");
			candidate.impl_->factions.validate(value.at("faction"));
			entity.set<FactionMembership>({value.at("faction")});
		}
		if (const auto *craft = entity.try_get<CraftState>();
		    craft && candidate.impl_->captured_by.contains(craft->home))
			entity.set<FactionMembership>(candidate.impl_->entities.at(craft->home).get<FactionMembership>());
		if (value.value("faction", std::string{}) !=
		    (entity.try_get<FactionMembership>() ? entity.get<FactionMembership>().id : std::string{}))
			throw std::runtime_error("Saved faction differs from fixed map membership");
		if (entity.has<Destructible>())
		{
			const auto d = value.at("destructible").get<Destructible>();
			if (Json(d.definition) != Json(entity.get<Destructible>().definition) || d.durability < 0 ||
			    d.durability > candidate.impl_->definitions.at(restored.definition).durability ||
			    d.destroyed_tick < -1 || d.destroyed_tick > saved.at("tick").get<std::int64_t>() ||
			    ((d.processed || d.rule_notified) && d.destroyed_tick < 0) || (d.processed && !d.rule_notified))
				throw std::runtime_error("Invalid destruction continuation");
			entity.set<Destructible>(d);
		}
		if (auto *stocks = entity.try_get_mut<ShipStockStores>())
		{
			const auto &saved_stocks = value.at("stocks");
			if (!saved_stocks.is_object() || saved_stocks.size() != stocks->stores.size())
				throw std::runtime_error("Saved stock roster mismatch");
			for (auto &[key, stock] : stocks->stores)
				stock.restore(saved_stocks.at(key));
		}
		else if (value.contains("stocks"))
			throw std::runtime_error("Unexpected saved stocks");
		if (entity.has<CombatStructure>())
			candidate.impl_->restore_combat(entity, value.at("combat"));
		if (entity.has<ShieldComponent>())
			candidate.impl_->restore_shields(entity, value.at("shields"));
		if (!candidate.impl_->definition(restored.id).personnel.officers.empty() && !value.contains("personnel"))
			throw std::runtime_error("Missing saved personnel");
		if (value.contains("personnel"))
			candidate.impl_->restore_personnel(restored.id, value.at("personnel"));
		if (value.contains("bridge"))
			candidate.impl_->restore_bridge(restored.id, value.at("bridge"));
		if (entity.has<EntityControls>())
		{
			candidate.impl_->restore_controllers(entity, value.at("console_controllers"));
		}
		else if (value.contains("console_controllers"))
			throw std::runtime_error("Unexpected saved Controller layout");

		validate_checkpoint_body(restored, entity.get<Definition>().name, entity.has<Movable>());
		const auto saved_altitude = value.value("body_altitude", std::int64_t{0});
		validate_checkpoint_altitude(saved_altitude);
		entity.set<Pose>({restored.position, restored.heading, saved_altitude, saved_altitude})
		    .set<Motion>({restored.velocity, restored.remainder});
		if (candidate.impl_->definition(restored.id).craft)
		{
			const auto v = value.at("vertical").get<VerticalMotion>();
			validate_checkpoint_vertical(v);
			entity.set<VerticalMotion>(v);
			entity.get_mut<Pose>().altitude = v.altitude;
			entity.get_mut<Pose>().previous_altitude = v.altitude;
			entity.get_mut<Motion>().vertical_velocity = v.velocity;
		}
		if (auto systems = fittings(entity))
		{
			auto states = value.at("systems");
			if (!states.is_object() || states.size() != systems->values.size())
				throw std::runtime_error("Fitted System roster mismatch");
			for (auto system : systems->values)
			{
				if (!states.at(system.id).contains("damping_remainder"))
					states.at(system.id)["damping_remainder"] = 0;
				for (const auto *field :
				     {"speed", "heading", "acceleration_remainder", "damping_remainder", "turn_remainder"})
					if (!states.at(system.id).at(field).is_number_integer())
						throw std::runtime_error("Fitted continuation requires integer state");
				const auto state = states.at(system.id).get<SavedOperation>();
				if (system.advanced)
				{
					SavedOperation expected;
					expected.heading = restored.heading;
					if (Json(expected) != states.at(system.id))
						throw std::runtime_error("Advanced System has unsupported legacy fields");
					continue;
				}
				if (Json(state) != states.at(system.id) ||
				    (!system.engine && (state.speed || state.acceleration_remainder || state.damping_remainder ||
				                        !state.engine_enabled)) ||
				    (!system.steering && (state.heading != restored.heading || state.turn_remainder)) ||
				    (!system.radar && !state.radar_enabled))
					throw std::runtime_error("Unsupported fitted System continuation fields");
				if (state.speed < 0 || state.heading < 0 || state.heading >= 360000 ||
				    state.acceleration_remainder < 0 || state.acceleration_remainder >= rate ||
				    state.damping_remainder < 0 || state.damping_remainder >= rate || state.turn_remainder < 0 ||
				    state.turn_remainder >= rate || (system.engine && state.speed > system.engine->max_speed))
					throw std::runtime_error("Invalid fitted System continuation");
				if (system.engine)
				{
					system.engine->speed = state.speed;
					system.engine->enabled = state.engine_enabled;
					system.engine->remainder = state.acceleration_remainder;
					system.engine->damping_remainder = state.damping_remainder;
				}
				if (system.steering)
				{
					system.steering->heading = state.heading;
					system.steering->remainder = state.turn_remainder;
				}
				if (system.radar)
					system.radar->enabled = state.radar_enabled;
			}
		}
		if (auto systems = gameplay_systems(entity))
		{
			const auto &data = value.at("gameplay");
			if (!data.is_array() || data.size() != systems->values.size())
				throw std::runtime_error("Advanced System roster mismatch");
			for (std::size_t i = 0; i < data.size(); ++i)
			{
				auto system = systems->values[i];
				const auto &state = data[i];
				if (state.at("id") != (*fittings(entity)).values[i].id ||
				    state.contains("flight") != (system.flight != nullptr) || state.contains("brain") ||
				    state.contains("timed") || state.contains("summary") || state.contains("effects"))
					throw std::runtime_error("Advanced continuation binding mismatch");
				if (system.flight)
				{
					system.flight->state = load_flight(system.flight->definition, state.at("flight"));
					if (system.flight->state.observed_tick > saved.at("tick").get<std::uint64_t>())
						throw std::runtime_error("Saved flight observation is in the future");
				}
				system.heat_remainder = state.at("heat_remainder");
				if (system.heat_remainder < 0 || system.heat_remainder >= rate)
					throw std::runtime_error("Invalid operating heat remainder");
				(*fittings(entity)).values[i].enabled = state.at("enabled");
				if (auto *ftl = entity.get<EquipmentIndex>().values.at(i).try_get_mut<FtlReserve>())
				{
					ftl->discharge_remainder =
					    projection::number(state.value("ftl_discharge_remainder", Json(0)), 0, rate - 1);
				}
				if (auto radar = (*fittings(entity)).values[i].radar)
					radar->enabled = state.at("radar_enabled").get<bool>();
			}
			if (systems->power)
				restore_ship_resources(*systems->power, value.at("resources"));
		}
	}
	{
		candidate.impl_->knowledge.restore(saved.at("knowledge"));
		if (saved.at("knowledge").at("ships").size() != candidate.impl_->entities.size())
			throw std::runtime_error("Knowledge/physical roster count mismatch");
		for (const auto &row : saved.at("knowledge").at("ships"))
		{
			const auto id = row.at("ship").get<EntityId>();
			if (!candidate.impl_->entities.contains(id) ||
			    row.at("definition") != candidate.impl_->entities.at(id).get<Definition>().name)
				throw std::runtime_error("Knowledge/physical roster mismatch");
		}
		for (const auto &entry : saved.at("away"))
		{
			auto a = entry.at("address").get<ConsoleAddress>();
			if (!candidate.impl_->entities.contains(a.ship) || !candidate.impl_->seat(a.ship, a.console))
				throw std::runtime_error("Unknown Away Console");
			candidate.impl_->ownership.set_away(a, entry.at("value").get<bool>());
		}
	}
	// Restore physical field entities and their index before display/Comms
	// reconstruction can capture a Console observation.
	if (saved.contains("asteroid_fields"))
		candidate.impl_->restore_fields(saved.at("asteroid_fields"));
	else
		candidate.impl_->stream_fields();
	candidate.impl_->index_contacts.run();
	for (const auto &[console, address] : saved.at("claims").get<std::map<std::uint64_t, ConsoleAddress>>())
	{
		const auto *seat = candidate.impl_->entities.contains(address.ship)
		                       ? candidate.impl_->seat(address.ship, address.console)
		                       : nullptr;
		if (!seat || !candidate.can_claim(console, address.ship, address.console, seat->human_seeking))
			throw std::runtime_error("Saved state invalid claim");
		if (!candidate.impl_->ownership.claim(console, address, seat->human_seeking))
			throw std::runtime_error("Saved state invalid claim");
	}

	candidate.impl_->display_revision =
	    static_cast<std::uint64_t>(display::number(saved.value("display_revision", Json(0)), 0, 9007199254740991LL));
	for (const auto &d : saved.value("displays", Json::array()))
	{
		const auto claimant = d.at("claimant").get<std::uint64_t>();
		const auto address = d.at("address").get<ConsoleAddress>();
		OperatingCommand command{address.ship, "display.toggle", 0, d.at("widget")};
		command.console = address.console;
		command.arguments = d.at("camera").get<std::vector<std::int64_t>>();
		if (!candidate.impl_->display_command(claimant, command, false) ||
		    candidate.impl_->displays.contains(address.ship))
			throw std::runtime_error("Invalid saved display source");
		const auto revision = display::number(d.at("revision"), 1, candidate.impl_->display_revision);
		candidate.impl_->displays[address.ship] = {claimant, static_cast<std::uint64_t>(revision), address,
		                                           command.system, command.arguments};
	}

	for (const auto &[id, entity] : candidate.impl_->entities)
		if (entity.has<EntityControls>())
			candidate.impl_->refresh_control(id, false);
	auto tick = saved.at("tick").get<std::int64_t>();
	if (tick < 0)
		throw std::runtime_error("Saved state invalid tick");
	candidate.impl_->ecs.get_mut<Clock>().tick = static_cast<std::uint64_t>(tick);
	candidate.impl_->environment_tick = saved.value("environment_tick", static_cast<std::uint64_t>(tick));
	candidate.impl_->refresh_comms();
	candidate.impl_->conversations.restore(saved.at("conversations"));
	candidate.impl_->restore_craft(saved.value("craft", Json()));
	candidate.impl_->restore_away(saved.value("away_missions", Json::array()));
	candidate.impl_->personnel_postings =
	    saved.value("personnel_postings", decltype(candidate.impl_->personnel_postings){});
	candidate.impl_->restore_interdictions(saved.value("interdictions", Json::array()), content);
	candidate.impl_->departures.restore_groups(saved.value("departure_groups", Json(DepartureGroups{})));
	candidate.impl_->departures.validate_group_members(
	    [&candidate](EntityId id)
	    {
		    return candidate.impl_->entities.contains(id);
	    });
	candidate.impl_->discard_obsolete_encounter_departures();
	candidate.impl_->departures.validate_group_authority(
	    [&candidate](EntityId leader, EntityId member)
	    {
		    return candidate.impl_->departure_member_authorised(leader, member);
	    });
	candidate.impl_->validate_personnel_postings();

	for (const auto &[target, origin] : candidate.impl_->captured_by)
	{
		bool receipt = false;
		for (std::size_t i = 0; i < candidate.impl_->away_operations.definitions().size(); ++i)
			receipt |= candidate.impl_->away_operations.definitions()[i].target == target &&
			           candidate.impl_->operational_owner(
			               candidate.impl_->away_ship(candidate.impl_->away_operations.definitions()[i])) ==
			               candidate.impl_->operational_owner(origin) &&
			           candidate.impl_->away_operations.missions()[i].boarding.captured;
		if (!receipt)
			for (const auto &[ship, postings] : candidate.impl_->personnel_postings)
				for (const auto &[asset, posting] : postings)
					receipt |= posting.target == target && posting.status == "prize_crew" &&
					           candidate.impl_->operational_owner(ship) == candidate.impl_->operational_owner(origin);
		if (!receipt)
			receipt =
			    candidate.impl_->interdiction_book.capture_receipt(target, candidate.impl_->operational_owner(origin));
		if (!receipt)
			throw std::runtime_error("Capture without ownership receipt");
		(void)candidate.impl_->operational_owner(target);
	}
	for (const auto &[id, entity] : candidate.impl_->entities)
		if (!candidate.impl_->captured_by.contains(id))
		{
			if (entity.has<PersonnelState>())
				for (const auto &o : entity.get<PersonnelState>().officers)
					if (o.detained &&
					    !candidate.impl_->captured_posting(
					        id, "officer:" + candidate.impl_->definition(id)
					                             .personnel.officers[&o - entity.get<PersonnelState>().officers.data()]
					                             .id))
						throw std::runtime_error("Detention without capture");
			if (entity.has<EquipmentIndex>())
				for (auto child : entity.get<EquipmentIndex>().values)
					if (const auto *mechanism = child.try_get<CombatMechanism>();
					    mechanism && mechanism->definition.kind == OperatingSystem::Team &&
					    mechanism->get<TeamState>().detained &&
					    !candidate.impl_->captured_posting(id, "team:" + child.get<Equipment>().id))
						throw std::runtime_error("Team detention without capture");
		}
	candidate.impl_->validate_craft_transports();
	candidate.impl_->restore_docking(saved);
	candidate.impl_->validate_interdiction_physics();
	candidate.impl_->restore_logistics(saved);
	for (const auto &body : saved.at("bodies"))
	{
		auto entity = candidate.impl_->entities.at(body.at("body").at("id").get<EntityId>());
		if (entity.has<ShipProcessors>())
			candidate.impl_->restore_processors(entity, body.at("processors"));
		else if (body.contains("processors"))
			throw std::runtime_error("Unexpected saved processors");
	}
	candidate.impl_->validate_stock_claim_owners();
	candidate.impl_->mission_ledger = saved.value("mission_ledger", MissionLedger{});
	std::map<EntityId, Json> departure_reports;
	std::set<EntityId> departed_ships;
	for (const auto &value : saved.at("bodies"))
		if (value.contains("mission_report"))
		{
			const auto id = value.at("body").at("id").get<EntityId>();
			const auto report = ftl_projection::report(value.at("mission_report"));
			if (report.is_null() || report.at("tick") > saved.at("tick") ||
			    (report.at("reason") == "ftl" ? candidate.impl_->definition(id).ftl.empty()
			                                  : !candidate.impl_->truly_destroyed(id)))
				throw std::runtime_error("Invalid saved departure");
			departure_reports.emplace(id, report);
			departed_ships.insert(id);
		}
	candidate.impl_->suppress_custom_property_events = true;
	candidate.impl_->restore_custom_properties(saved.at("custom_properties"));
	candidate.impl_->restore_active_modifiers(saved.at("active_effects"));
	candidate.impl_->restore_abilities(saved.at("abilities"), departed_ships);
	candidate.impl_->restore_processes(saved.at("processes"), departed_ships);
	candidate.impl_->refresh_ability_work(false);
	candidate.impl_->restore_psychic(saved.value("psychic", Json::object()), departed_ships);
	candidate.impl_->restore_sensors(saved.value("sensors", Json::array()));
	candidate.impl_->validate_directed_commitments();
	candidate.impl_->sensor_findings.restore_discoveries(
	    saved.value("sensor_discoveries", SensorFindings::Discoveries{}));
	candidate.impl_->restore_conversation_starts(saved.at("conversation_starts"));
	candidate.impl_->restore_projectiles(saved);
	candidate.impl_->restore_rules(saved.at("rules"), saved.at("active_effects").at("conditions"),
	                               saved.at("active_effects").at("next_condition"));
	candidate.impl_->suppress_custom_property_events = false;

	candidate.impl_->blast_visuals = saved.value("blast_visuals", Json::array()).get<std::vector<BlastVisual>>();
	candidate.impl_->next_blast = saved.value("next_blast", std::uint64_t{1});
	for (const auto &[id, report] : departure_reports)
	{
		candidate.impl_->entities.at(id).set<MissionDeparture>({report});
		if (report.at("reason") == "ftl")
			candidate.impl_->retire_departed(id);
	}
	candidate.impl_->index_contacts.run();
	candidate.impl_->refresh_comms(false);
	candidate.impl_->validate_mission_ledger();
	candidate.impl_->validate_medical_state();
	for (const auto &[id, entity] : candidate.impl_->entities)
		if (entity.has<BridgeBlackboard>())
			candidate.impl_->reconcile_objective_priority(id);
	impl_ = std::move(candidate.impl_);
}
} // namespace phoenix
