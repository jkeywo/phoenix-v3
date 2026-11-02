module;
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <set>
#include <string>
#include <utility>
#include <vector>

module phoenix.workshop.gameplay;
import phoenix.workshop.authoring_core;
import phoenix.content.documents;
import phoenix.workshop.authoring;
import phoenix.workshop.tree;
import phoenix.workshop.test_client;

namespace phoenix
{
#include "state.hpp"
void GameplayEditors::State::actor(Json &parent, const char *key, const std::vector<std::string> &slots)
{
	ScopedId id_scope_22(key);
	heading(key);
	edit_shared_control(label("Actor source").c_str(), parent, "actor.source", {{"field", key}});
	if (parent.contains(key))
	{
		if (parent.at(key).contains("slot"))
			choice(parent[key], "slot", slots);
		else if (parent.at(key).contains("entity"))
			edit_registered(parent[key], "gameplay-editors.scenarios.entity");
		else if (parent.at(key).contains("participant"))
			edit_registered(parent[key], "gameplay-editors.scenarios.participant");
		else
			edit_registered(parent[key], "gameplay-editors.scenarios.handle");
	}
	else if (button("Bind actor"))
		(void)workshop_custom_action(parent, "actor.bind", {{"field", key}, {"slots", slots}});
}

void GameplayEditors::State::scenario(WorkshopDocument &document)
{
	records(document.definitions(), "tasks", workshop_record("gameplay-editors.scenarios.tasks"),
	        [&](Json &task)
	        {
		        structure(task);
	        });
	auto &map = document.map();
	optional_string(map, "briefing_key");
	if (map.contains("ship_briefings"))
		structure(map["ship_briefings"]);
	else if (button("Add ship briefings"))
		map["ship_briefings"] = workshop_seed("gameplay-editors.scenarios.ship_briefings");
	records(map, "factions", workshop_record("gameplay-editors.scenarios.factions"),
	        [&](Json &f)
	        {
		        edit_registered(f, "gameplay-editors.scenarios.id");
		        edit_registered(f, "gameplay-editors.scenarios.label");
	        });
	const auto faction_ids = map.contains("factions") ? ids(map.at("factions")) : std::vector<std::string>{};
	records(map, "relationships", workshop_record("gameplay-editors.scenarios.relationships"),
	        [&](Json &r)
	        {
		        choice(r, "a", faction_ids);
		        choice(r, "b", faction_ids);
		        edit_registered(r, "gameplay-editors.scenarios.relation");
	        });
	records(map, "sensor_findings", workshop_record("gameplay-editors.scenarios.sensor_findings"),
	        [&](Json &f)
	        {
		        edit_registered(f, "gameplay-editors.scenarios.id");
		        edit_registered(f, "gameplay-editors.scenarios.text_key");
		        edit_registered(f, "gameplay-editors.scenarios.target_entity");
		        edit_registered(f, "gameplay-editors.scenarios.scan_ticks");
		        strings(f, "requires");
		        optional_string(f, "requires_event");
		        optional_string(f, "unless_event");
		        optional_string(f, "discovery_event");
		        optional_string(f, "tag");
		        optional_string(f, "destination_station");
	        });
	records(map, "starting_stocks", workshop_record("gameplay-editors.scenarios.starting_stocks"),
	        [&](Json &setup)
	        {
		        edit_registered(setup, "gameplay-editors.scenarios.ship_slot");
		        choice(setup, "definition", document.definition_choices(WorkshopDefinitionKind::Entity));
		        for (const auto *field : {"requires_cargo_shuttle", "shuttle_material", "shuttle_quantity"})
			        edit_registered(setup, (std::string("gameplay-editors.scenarios.") + field).c_str());
		        structured_field(setup, "stores");
	        });
	records(map, "interdictions", workshop_record("gameplay-editors.scenarios.interdictions"),
	        [&](Json &m)
	        {
		        edit_registered(m, "gameplay-editors.scenarios.id");
		        edit_registered(m, "gameplay-editors.scenarios.player_slot");
		        edit_registered(m, "gameplay-editors.scenarios.target_entity");
		        edit_registered(m, "gameplay-editors.scenarios.start_event");
		        optional_string(m, "target_character");
		        optional_string(m, "attacker_character");
		        tree_editor.tree_binding(document, m, "tree", "scenario", false);
		        tree_editor.tree_binding(document, m, "dialogue", "conversation", false);
		        edit_shared_group(m, "scenario.engagement");
		        strings(m, "occupation_teams");
		        if (!m.contains("takeover_effects"))
			        m["takeover_effects"] = workshop_seed("gameplay-editors.scenarios.takeover_effects");
		        auto &effects = m["takeover_effects"];
		        edit_registered(effects, "gameplay-editors.scenarios.repair");
		        edit_registered(effects, "gameplay-editors.scenarios.original_crew");
		        edit_registered(effects, "gameplay-editors.scenarios.capture_visitors");
		        edit_registered(m, "gameplay-editors.scenarios.fleet_variant");
		        edit_registered(m, "gameplay-editors.scenarios.plan_event");
		        strings(m, "concealed_variants");
		        structured_field(m, "plan_flight_objectives");
		        if (!m.contains("weapon_objectives"))
			        m["weapon_objectives"] = workshop_seed("gameplay-editors.scenarios.weapon_objectives");
		        structured_field(m, "weapon_objectives");
		        for (const auto *field : {"flight_objectives", "rendezvous", "departure"})
			        structured_field(m, field);
		        structured_field(m, "variants");
		        structured_field(m, "pleas");
	        });
	records(map, "away_missions", workshop_record("gameplay-editors.scenarios.away_missions"),
	        [&](Json &m)
	        {
		        edit_registered(m, "gameplay-editors.scenarios.id");
		        edit_registered(m, "gameplay-editors.scenarios.title_key");
		        edit_registered(m, "gameplay-editors.scenarios.ship_slot");
		        edit_registered(m, "gameplay-editors.scenarios.target_entity");
		        edit_registered(m, "gameplay-editors.scenarios.target_definition");
		        tree_editor.tree_binding(document, m, "hail_tree", "conversation", true);
		        edit_registered(m, "gameplay-editors.scenarios.conversation");
		        edit_registered(m, "gameplay-editors.scenarios.reveal_event");
		        edit_registered(m, "gameplay-editors.scenarios.retain_craft");
		        tree_editor.tree_binding(document, m, "tree", "away", false);
		        edit_registered(m, "gameplay-editors.scenarios.allow_ship_transport");
		        strings(m, "docking_ports");
		        optional_string(m, "ship_transport_reason");
		        edit_registered(m, "gameplay-editors.scenarios.deployment_range_mm");
		        edit_registered(m, "gameplay-editors.scenarios.deployment_ticks");
		        records(m, "slots", workshop_record("gameplay-editors.scenarios.slots"),
		                [&](Json &s)
		                {
			                edit_registered(s, "gameplay-editors.scenarios.id");
			                edit_registered(s, "gameplay-editors.scenarios.kind");
			                optional_string(s, "trait");
			                optional_string(s, "team_type");
			                optional_string(s, "capability");
			                edit_registered(s, "gameplay-editors.scenarios.required");
			                edit_registered(s, "gameplay-editors.scenarios.assign_ai");
		                });
	        });
	records(map, "conversations", workshop_record("gameplay-editors.scenarios.conversations"),
	        [&](Json &c)
	        {
		        edit_registered(c, "gameplay-editors.scenarios.id");
		        tree_editor.tree_binding(document, c, "tree", "conversation", false);
		        optional_string(c, "title_key");
		        optional_string(c, "start_event");
		        strings(c, "cancel_events");
		        strings(c, "members");
		        strings(c, "events");
		        structured_field(c, "bindings");
	        });
	records(map, "regions", workshop_record("gameplay-editors.scenarios.regions"),
	        [&](Json &region)
	        {
		        edit_registered(region, "gameplay-editors.scenarios.id");
		        edit_registered(region, "gameplay-editors.scenarios.shape");
		        edit_registered(region, "gameplay-editors.scenarios.x_mm");
		        edit_registered(region, "gameplay-editors.scenarios.y_mm");
		        if (region.value("shape", std::string("circle")) != "rectangle")
			        edit_registered(region, "gameplay-editors.scenarios.radius_mm");
		        else
		        {
			        edit_registered(region, "gameplay-editors.scenarios.half_width_mm");
			        edit_registered(region, "gameplay-editors.scenarios.half_height_mm");
		        }
		        if (region.value("shape", std::string("circle")) == "annulus")
			        edit_registered(region, "gameplay-editors.scenarios.inner_radius_mm");
		        edit_shared_field(label("Map marker").c_str(), region, "region.display");
		        const bool display = region.contains("display");

		        if (display)
		        {
			        auto &d = region["display"];
			        edit_registered(d, "gameplay-editors.scenarios.name");
			        strings(d, "tags");
			        edit_shared_field(label("navigation").c_str(), d, "region.display.navigation");
			        edit_shared_field(label("radar").c_str(), d, "region.display.radar");
			        edit_shared_field(label("Map marker colour").c_str(), d, "region.display.colour");
		        }
		        edit_shared_field(label("Nebula").c_str(), region, "region.nebula");
		        const bool fog = region.contains("nebula");

		        if (fog)
		        {
			        auto &n = region["nebula"];
			        edit_shared_field(label("density").c_str(), n, "region.nebula.density");
			        edit_shared_field(label("sensor_factor").c_str(), n, "region.nebula.sensor_factor");
			        edit_registered(n, "gameplay-editors.scenarios.half_height_mm.2");
			        edit_shared_field(label("Nebula colour").c_str(), n, "region.nebula.colour");
		        }
		        edit_shared_field(label("Asteroid field").c_str(), region, "region.asteroids");
		        const bool asteroids = region.contains("asteroids");

		        if (asteroids)
		        {
			        auto &a = region["asteroids"];
			        edit_shared_group(a, "asteroids");
			        edit_shared_field(label("Density (rocks per square metre)").c_str(), a, "region.asteroids.density");

			        for (const char *key : {"types", "decorative_types"})
				        records(a, key, workshop_record("gameplay-editors.scenarios.field_types"),
				                [&](Json &type)
				                {
					                choice(type, "definition",
					                       document.definition_choices(WorkshopDefinitionKind::Entity));
					                edit_registered(type, "gameplay-editors.scenarios.sprite_entry");
					                edit_registered(type, "gameplay-editors.scenarios.weight");
				                });
		        }
	        });
	if (!map.contains("scenario"))
	{
		if (button("Create scenario"))
			map["scenario"] = workshop_seed("gameplay-editors.scenarios.scenario");
		return;
	}
	auto &scenario = map["scenario"];
	if (scenario.contains("task_plan"))
		structure(scenario["task_plan"]);
	else if (button("Add task plan"))
		scenario["task_plan"] = workshop_seed("gameplay-editors.scenarios.task_plan");
	tree_editor.tree_binding(document, scenario, "tree", "scenario", true);
	scalar_map(scenario, "parameters");
	strings(scenario, "events");
	strings(scenario, "publications");
	const auto slots = map.contains("ship_slots") ? ids(map.at("ship_slots")) : std::vector<std::string>{};
	if (ScopedTree section(label("Main scenario selectors").c_str()); section)
	{
		if (scenario.contains("bindings"))
			scenario_selectors(scenario["bindings"], scenario, map, slots);
		else if (button("Configure main selectors"))
			scenario["bindings"] = workshop_seed("gameplay-editors.scenarios.bindings");
	}
	records(scenario, "situations", workshop_record("gameplay-editors.scenarios.situations"),
	        [&](Json &situation)
	        {
		        edit_registered(situation, "gameplay-editors.scenarios.id");
		        tree_editor.tree_binding(document, situation, "tree", "scenario", false);
		        scalar_map(situation, "parameters");
		        if (situation.contains("bindings"))
			        scenario_selectors(situation["bindings"], scenario, map, slots);
		        else if (button("Configure situation selectors"))
			        situation["bindings"] = workshop_seed("gameplay-editors.scenarios.bindings");
		        edit_registered(situation, "gameplay-editors.scenarios.start");
		        optional_string(situation, "instance_name");
	        });
	records(scenario, "spawn_points", workshop_record("gameplay-editors.scenarios.spawn_points"),
	        [&](Json &point)
	        {
		        edit_registered(point, "gameplay-editors.scenarios.id");
		        edit_registered(point, "gameplay-editors.scenarios.x_mm");
		        edit_registered(point, "gameplay-editors.scenarios.y_mm");
	        });
	records(scenario, "spawn_groups", workshop_record("gameplay-editors.scenarios.spawn_groups"),
	        [&](Json &group)
	        {
		        edit_registered(group, "gameplay-editors.scenarios.id");
		        records(group, "entities", workshop_record("gameplay-editors.scenarios.entities"),
		                [&](Json &entity)
		                {
			                choice(entity, "definition", document.definition_choices(WorkshopDefinitionKind::Entity));
			                edit_registered(entity, "gameplay-editors.scenarios.x_mm");
			                edit_registered(entity, "gameplay-editors.scenarios.y_mm");
			                edit_registered(entity, "gameplay-editors.scenarios.heading_mdeg");
		                });
	        });
	const auto regions = map.contains("regions") ? ids(map.at("regions")) : std::vector<std::string>{};
	records(scenario, "objectives", workshop_record("gameplay-editors.scenarios.objectives"),
	        [&](Json &objective)
	        {
		        edit_registered(objective, "gameplay-editors.scenarios.id");
		        edit_registered(objective, "gameplay-editors.scenarios.type");
		        edit_registered(objective, "gameplay-editors.scenarios.behavior");
		        edit_registered(objective, "gameplay-editors.scenarios.tier");
		        edit_registered(objective, "gameplay-editors.scenarios.scoring_policy");
		        edit_registered(objective, "gameplay-editors.scenarios.urgency");
		        edit_registered(objective, "gameplay-editors.scenarios.operation");
		        edit_registered(objective, "gameplay-editors.scenarios.compartment");
		        edit_registered(objective, "gameplay-editors.scenarios.system");
		        edit_registered(objective, "gameplay-editors.scenarios.mission");
		        edit_registered(objective, "gameplay-editors.scenarios.report_as");
		        actor(objective, "behavior_target", slots);
		        edit_registered(objective, "gameplay-editors.scenarios.optional");
		        if (objective.value("type", "") == "condition")
		        {
			        if (!objective.contains("complete_when"))
				        objective["complete_when"] = workshop_seed("gameplay-editors.scenarios.complete_when");
			        structure(objective["complete_when"]);
			        if (objective.contains("fail_when"))
				        structure(objective["fail_when"]);
			        else if (button("Add failure condition"))
				        objective["fail_when"] = workshop_seed("gameplay-editors.scenarios.fail_when");
		        }
		        else
			        choice(objective, "region", regions);
		        actor(objective, "actor", slots);
		        edit_registered(objective, "gameplay-editors.scenarios.title_key");
		        edit_registered(objective, "gameplay-editors.scenarios.description_key");
		        edit_registered(objective, "gameplay-editors.scenarios.competition");
		        if (!objective.contains("recipients"))
		        {
			        if (button("Bind objective recipients"))
				        (void)workshop_custom_action(objective, "objective.recipients", {{"slots", slots}});
		        }
		        else
		        {
			        actor(objective["recipients"], "ship", slots);
			        strings(objective["recipients"], "consoles");
		        }
	        });
	records(scenario, "fact_bindings", workshop_record("gameplay-editors.scenarios.fact_bindings"),
	        [&](Json &f)
	        {
		        structure(f);
	        });
	records(scenario, "event_bindings", workshop_record("gameplay-editors.scenarios.event_bindings"),
	        [&](Json &e)
	        {
		        structure(e);
	        });
	records(scenario, "reports", workshop_record_value("scenario.report", {{"slots", slots}}),
	        [&](Json &r)
	        {
		        structure(r);
	        });
	records(scenario, "objective_sequences", workshop_record("gameplay-editors.scenarios.objective_sequences"),
	        [&](Json &sequence)
	        {
		        edit_registered(sequence, "gameplay-editors.scenarios.id");
		        strings(sequence, "objectives");
		        edit_registered(sequence, "gameplay-editors.scenarios.repeat");
	        });
	if (ScopedTree scope(label("Scenario limits").c_str()); scope)
	{
		if (!scenario.contains("limits"))
		{
			if (button("Override default limits"))
				scenario["limits"] = workshop_seed("gameplay-editors.scenarios.limits");
		}
		else
		{
			edit_registered(scenario["limits"], "gameplay-editors.scenarios.spawn_per_tick");
			edit_registered(scenario["limits"], "gameplay-editors.scenarios.spawned_population");
			edit_registered(scenario["limits"], "gameplay-editors.scenarios.instances");
			edit_registered(scenario["limits"], "gameplay-editors.scenarios.actions");
			edit_registered(scenario["limits"], "gameplay-editors.scenarios.objectives");
			edit_registered(scenario["limits"], "gameplay-editors.scenarios.events");
		}
	}
	text("Objective definitions become active only when a scenario action adds them. Reach-region completion uses "
	     "actual executed movement, including a crossing without stopping.");
}

void GameplayEditors::State::scenario_selectors(Json &bindings, const Json &scenario, const Json &map,
                                                const std::vector<std::string> &slots)
{
	actor(bindings, "actor", slots);
	optional_string(bindings, "knowledge_console");
	edit_registered(bindings, "gameplay-editors.scenarios.objective");
	choice(bindings, "region", map.contains("regions") ? ids(map.at("regions")) : std::vector<std::string>{}, true);
	std::vector<std::string> events;
	if (scenario.contains("events"))
		for (const auto &event : scenario.at("events"))
			events.push_back(event.get<std::string>());
	choice(bindings, "event", events, true);
}
} // namespace phoenix
