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
	const auto value = parent.contains(key) ? parent.at(key) : Json::object();
	Json kind{{"Actor source", value.contains("slot") ? "slot" : value.contains("entity") ? "entity" : "handle"}};
	if (edit_registered(kind, "gameplay-editors.scenarios.Actor source"))
	{
		const auto selected = kind.at("Actor source").get<std::string>();
		parent[key] = selected == "entity" ? Json{{"entity", 1}} : Json{{selected, ""}};
	}
	if (parent.contains(key))
	{
		if (parent.at(key).contains("slot"))
			choice(parent[key], "slot", slots);
		else if (parent.at(key).contains("entity"))
			edit_registered(parent[key], "gameplay-editors.scenarios.entity");
		else
			edit_registered(parent[key], "gameplay-editors.scenarios.handle");
	}
	else if (button("Bind actor"))
		parent[key] = {{"slot", slots.empty() ? "" : slots.front()}};
}

void GameplayEditors::State::scenario(WorkshopDocument &document)
{
	auto &map = document.map();
	optional_string(map, "briefing_key");
	if (map.contains("ship_briefings"))
		structure(map["ship_briefings"]);
	else if (button("Add ship briefings"))
		map["ship_briefings"] = Json::object();
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
		        for (auto key : {"threat_radius_mm", "clearance_radius_mm", "engagement_radius_mm",
		                         "formation_radius_mm", "takeover_ticks", "combat_standoff_mm", "cruise_speed_mm_s"})
			        integer(m, key, 1);
		        strings(m, "occupation_teams");
		        if (!m.contains("takeover_effects"))
			        m["takeover_effects"] = {{"repair", true}, {"original_crew", "kill"}, {"capture_visitors", true}};
		        auto &effects = m["takeover_effects"];
		        edit_registered(effects, "gameplay-editors.scenarios.repair");
		        edit_registered(effects, "gameplay-editors.scenarios.original_crew");
		        edit_registered(effects, "gameplay-editors.scenarios.capture_visitors");
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
	records(
	    map, "regions", workshop_record("gameplay-editors.scenarios.regions"),
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
		    bool display = region.contains("display");
		    if (ImGui::Checkbox(label("Map marker").c_str(), &display))
		    {
			    if (display)
				    region["display"] = {{"name", region.value("id", "Region")},
				                         {"navigation", true},
				                         {"radar", true},
				                         {"colour", {.5, .7, .9}},
				                         {"tags", {"contact"}}};
			    else
				    region.erase("display");
		    }
		    if (display)
		    {
			    auto &d = region["display"];
			    edit_registered(d, "gameplay-editors.scenarios.name");
			    strings(d, "tags");
			    for (const auto *key : {"navigation", "radar"})
			    {
				    bool visible = d.value(key, false);
				    if (ImGui::Checkbox(label(key).c_str(), &visible))
					    d[key] = visible;
			    }
			    auto c = d.value("colour", Json::array({.5, .7, .9}));
			    float rgb[]{c[0], c[1], c[2]};
			    if (ImGui::ColorEdit3(label("Map marker colour").c_str(), rgb))
				    d["colour"] = {rgb[0], rgb[1], rgb[2]};
		    }
		    bool fog = region.contains("nebula");
		    if (ImGui::Checkbox(label("Nebula").c_str(), &fog))
		    {
			    if (fog)
				    region["nebula"] = {
				        {"density", .01}, {"sensor_factor", 1.0}, {"half_height_mm", 100000}, {"colour", {.5, .3, .8}}};
			    else
				    region.erase("nebula");
		    }
		    if (fog)
		    {
			    auto &n = region["nebula"];
			    for (const char *key : {"density", "sensor_factor"})
			    {
				    float value = n.value(key, 1.f);
				    if (ImGui::InputFloat(label(key).c_str(), &value))
					    n[key] = value;
			    }
			    edit_registered(n, "gameplay-editors.scenarios.half_height_mm.2");
			    const auto c = n.value("colour", Json::array({.5, .3, .8}));
			    float colour[3] = {c[0], c[1], c[2]};
			    if (ImGui::ColorEdit3(label("Nebula colour").c_str(), colour))
				    n["colour"] = {colour[0], colour[1], colour[2]};
		    }
		    bool asteroids = region.contains("asteroids");
		    if (ImGui::Checkbox(label("Asteroid field").c_str(), &asteroids))
		    {
			    if (asteroids)
				    region["asteroids"] = {
				        {"seed", 1}, {"spacing_mm", 25000}, {"density", .0004}, {"types", Json::array()}};
			    else
				    region.erase("asteroids");
		    }
		    if (asteroids)
		    {
			    auto &a = region["asteroids"];
			    for (const auto &[key, fallback] :
			         std::vector<std::pair<const char *, std::int64_t>>{{"seed", 1},
			                                                            {"spacing_mm", 25000},
			                                                            {"weight", 1000},
			                                                            {"jitter_mm", 8000},
			                                                            {"clustering_permille", 500},
			                                                            {"noise_cells", 8},
			                                                            {"decorative_height_mm", 20000},
			                                                            {"decorative_fill_permille", 250},
			                                                            {"spin_min_mdeg_s", 1000},
			                                                            {"spin_max_mdeg_s", 10000},
			                                                            {"visual_distance_mm", 250000},
			                                                            {"unload_margin_mm", 50000}})
				    integer(a, key, fallback);
			    float density = a.value("density", .0004f);
			    if (ImGui::InputFloat(label("Density (rocks per square metre)").c_str(), &density, .0001f, .001f,
			                          "%.6f"))
				    a["density"] = density;
			    for (const char *key : {"types", "decorative_types"})
				    records(a, key, {{"definition", ""}, {"sprite_entry", ""}, {"weight", 1000}},
				            [&](Json &type)
				            {
					            choice(type, "definition", document.definition_choices(WorkshopDefinitionKind::Entity));
					            edit_registered(type, "gameplay-editors.scenarios.sprite_entry");
					            edit_registered(type, "gameplay-editors.scenarios.weight");
				            });
		    }
	    });
	if (!map.contains("scenario"))
	{
		if (button("Create scenario"))
			map["scenario"] = Json::object();
		return;
	}
	auto &scenario = map["scenario"];
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
			scenario["bindings"] = Json::object();
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
			        situation["bindings"] = Json::object();
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
		        edit_registered(objective, "gameplay-editors.scenarios.optional");
		        if (objective.value("type", "") == "condition")
		        {
			        if (!objective.contains("complete_when"))
				        objective["complete_when"] = {{"fact", ""}, {"op", "eq"}, {"value", true}};
			        structure(objective["complete_when"]);
			        if (objective.contains("fail_when"))
				        structure(objective["fail_when"]);
			        else if (button("Add failure condition"))
				        objective["fail_when"] = {{"fact", ""}, {"op", "eq"}, {"value", true}};
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
				        objective["recipients"] = {{"ship", {{"slot", slots.empty() ? "" : slots.front()}}},
				                                   {"consoles", Json::array()}};
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
	records(
	    scenario, "reports",
	    {{"id", "report"},
	     {"ship", {{"slot", slots.empty() ? "" : slots.front()}}},
	     {"title_key", "Mission report"},
	     {"objectives", Json::array()},
	     {"facts", Json::array()},
	     {"rules", Json::array()},
	     {"fallback",
	      {{"result", "unfinished"}, {"title_key", "Unfinished"}, {"explanation_key", "Work remains unfinished."}}}},
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
				scenario["limits"] = Json::object();
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
