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
	if (choice(kind, "Actor source", {"slot", "entity", "handle"}))
	{
		const auto selected = kind.at("Actor source").get<std::string>();
		parent[key] = selected == "entity" ? Json{{"entity", 1}} : Json{{selected, ""}};
	}
	if (parent.contains(key))
	{
		if (parent.at(key).contains("slot"))
			choice(parent[key], "slot", slots);
		else if (parent.at(key).contains("entity"))
			integer(parent[key], "entity", 1);
		else
			string(parent[key], "handle");
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
	records(map, "factions", {{"id", "faction"}, {"label", "Faction"}},
	        [&](Json &f)
	        {
		        string(f, "id");
		        string(f, "label");
	        });
	const auto faction_ids = map.contains("factions") ? ids(map.at("factions")) : std::vector<std::string>{};
	records(map, "relationships", {{"a", ""}, {"b", ""}, {"relation", "neutral"}},
	        [&](Json &r)
	        {
		        choice(r, "a", faction_ids);
		        choice(r, "b", faction_ids);
		        choice(r, "relation", {"friendly", "neutral", "hostile"});
	        });
	records(map, "sensor_findings",
	        {{"id", "finding"},
	         {"text_key", "Sensor finding"},
	         {"target_entity", 1},
	         {"scan_ticks", 100},
	         {"requires", {"findings"}}},
	        [&](Json &f)
	        {
		        string(f, "id");
		        string(f, "text_key");
		        integer(f, "target_entity", 1);
		        integer(f, "scan_ticks", 100);
		        strings(f, "requires");
		        optional_string(f, "requires_event");
		        optional_string(f, "unless_event");
		        optional_string(f, "discovery_event");
		        optional_string(f, "tag");
		        optional_string(f, "destination_station");
	        });
	records(map, "starting_stocks", {{"ship_slot", ""}, {"definition", ""}, {"stores", Json::object()}},
	        [&](Json &setup)
	        {
		        string(setup, "ship_slot");
		        choice(setup, "definition", document.definition_choices(WorkshopDefinitionKind::Entity));
		        structured_field(setup, "stores");
	        });
	records(map, "interdictions",
	        {{"id", "encounter"},
	         {"player_slot", ""},
	         {"target_entity", 1},
	         {"start_event", ""},
	         {"tree", ""},
	         {"dialogue", ""},
	         {"threat_radius_mm", 600000},
	         {"clearance_radius_mm", 750000},
	         {"engagement_radius_mm", 2500000},
	         {"formation_radius_mm", 800000},
	         {"takeover_ticks", 2400},
	         {"variants", Json::object()},
	         {"pleas", Json::array()},
	         {"occupation_teams", Json::array()},
	         {"takeover_effects", {{"repair", false}, {"original_crew", "detain"}, {"capture_visitors", true}}}},
	        [&](Json &m)
	        {
		        string(m, "id");
		        string(m, "player_slot");
		        integer(m, "target_entity", 1);
		        string(m, "start_event");
		        optional_string(m, "target_character");
		        choice(m, "tree", document.definition_choices(WorkshopDefinitionKind::Tree));
		        choice(m, "dialogue", document.definition_choices(WorkshopDefinitionKind::Tree));
		        for (auto key : {"threat_radius_mm", "clearance_radius_mm", "engagement_radius_mm",
		                         "formation_radius_mm", "takeover_ticks", "combat_standoff_mm", "cruise_speed_mm_s"})
			        integer(m, key, 1);
		        strings(m, "occupation_teams");
		        if (!m.contains("takeover_effects"))
			        m["takeover_effects"] = {{"repair", true}, {"original_crew", "kill"}, {"capture_visitors", true}};
		        auto &effects = m["takeover_effects"];
		        boolean(effects, "repair", true);
		        choice(effects, "original_crew", {"kill", "detain"});
		        boolean(effects, "capture_visitors", true);
		        structured_field(m, "variants");
		        structured_field(m, "pleas");
	        });
	records(map, "away_missions",
	        {{"id", "mission"},
	         {"title_key", "Away mission"},
	         {"ship_slot", ""},
	         {"target_entity", 1},
	         {"conversation", ""},
	         {"reveal_event", ""},
	         {"tree", ""},
	         {"slots", Json::array()}},
	        [&](Json &m)
	        {
		        string(m, "id");
		        string(m, "title_key");
		        string(m, "ship_slot");
		        integer(m, "target_entity", 1);
		        string(m, "conversation");
		        string(m, "reveal_event");
		        boolean(m, "retain_craft", true);
		        choice(m, "tree", document.definition_choices(WorkshopDefinitionKind::Tree));
		        boolean(m, "allow_ship_transport", false);
		        strings(m, "docking_ports");
		        optional_string(m, "ship_transport_reason");
		        integer(m, "deployment_range_mm", 20000);
		        integer(m, "deployment_ticks", 20);
		        records(m, "slots", {{"id", "repair"}, {"kind", "team"}, {"required", true}},
		                [&](Json &s)
		                {
			                string(s, "id");
			                choice(s, "kind", {"team", "officer"});
			                optional_string(s, "trait");
			                optional_string(s, "team_type");
			                optional_string(s, "capability");
			                boolean(s, "required", true);
			                boolean(s, "assign_ai", false);
		                });
	        });
	records(map, "conversations",
	        {{"id", "conversation"}, {"tree", ""}, {"bindings", Json::object()}, {"members", Json::array()}},
	        [&](Json &c)
	        {
		        string(c, "id");
		        choice(c, "tree", document.definition_choices(WorkshopDefinitionKind::Tree));
		        optional_string(c, "title_key");
		        optional_string(c, "start_event");
		        strings(c, "cancel_events");
		        strings(c, "members");
		        strings(c, "events");
		        structured_field(c, "bindings");
	        });
	records(
	    map, "regions", {{"id", "region"}, {"shape", "circle"}, {"x_mm", 0}, {"y_mm", 0}, {"radius_mm", 10000}},
	    [&](Json &region)
	    {
		    string(region, "id");
		    choice(region, "shape", {"circle", "rectangle", "annulus"});
		    integer(region, "x_mm");
		    integer(region, "y_mm");
		    if (region.value("shape", std::string("circle")) != "rectangle")
			    integer(region, "radius_mm", 10000);
		    else
		    {
			    integer(region, "half_width_mm", 10000);
			    integer(region, "half_height_mm", 10000);
		    }
		    if (region.value("shape", std::string("circle")) == "annulus")
			    integer(region, "inner_radius_mm", 5000);
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
			    string(d, "name");
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
			    integer(n, "half_height_mm", 100000);
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
					            string(type, "sprite_entry");
					            integer(type, "weight", 1000);
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
	choice(scenario, "tree", document.definition_choices(WorkshopDefinitionKind::Tree), true);
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
	records(scenario, "situations",
	        {{"id", "situation"},
	         {"tree", ""},
	         {"parameters", Json::object()},
	         {"bindings", Json::object()},
	         {"start", false}},
	        [&](Json &situation)
	        {
		        string(situation, "id");
		        choice(situation, "tree", document.definition_choices(WorkshopDefinitionKind::Tree));
		        scalar_map(situation, "parameters");
		        if (situation.contains("bindings"))
			        scenario_selectors(situation["bindings"], scenario, map, slots);
		        else if (button("Configure situation selectors"))
			        situation["bindings"] = Json::object();
		        boolean(situation, "start");
		        optional_string(situation, "instance_name");
	        });
	records(scenario, "spawn_points", {{"id", "point"}, {"x_mm", 0}, {"y_mm", 0}},
	        [&](Json &point)
	        {
		        string(point, "id");
		        integer(point, "x_mm");
		        integer(point, "y_mm");
	        });
	records(scenario, "spawn_groups", {{"id", "group"}, {"entities", Json::array()}},
	        [&](Json &group)
	        {
		        string(group, "id");
		        records(group, "entities", {{"definition", ""}, {"x_mm", 0}, {"y_mm", 0}, {"heading_mdeg", 0}},
		                [&](Json &entity)
		                {
			                choice(entity, "definition", document.definition_choices(WorkshopDefinitionKind::Entity));
			                integer(entity, "x_mm");
			                integer(entity, "y_mm");
			                integer(entity, "heading_mdeg");
		                });
	        });
	const auto regions = map.contains("regions") ? ids(map.at("regions")) : std::vector<std::string>{};
	records(scenario, "objectives",
	        {{"id", "objective"}, {"type", "reach_region"}, {"region", ""}, {"title_key", ""}, {"description_key", ""}},
	        [&](Json &objective)
	        {
		        string(objective, "id");
		        choice(objective, "type", {"reach_region", "condition"});
		        boolean(objective, "optional");
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
		        string(objective, "title_key");
		        string(objective, "description_key");
		        string(objective, "competition");
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
	records(scenario, "fact_bindings",
	        {{"id", "fact"}, {"kind", "entity"}, {"subject", {{"entity", 1}}}, {"field", "lifecycle"}},
	        [&](Json &f)
	        {
		        structure(f);
	        });
	records(scenario, "event_bindings", {{"kind", "entity.departed"}, {"event", "event"}},
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
	records(scenario, "objective_sequences", {{"id", "sequence"}, {"objectives", Json::array()}, {"repeat", false}},
	        [&](Json &sequence)
	        {
		        string(sequence, "id");
		        strings(sequence, "objectives");
		        boolean(sequence, "repeat");
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
			integer(scenario["limits"], "spawn_per_tick", 64);
			integer(scenario["limits"], "spawned_population", 1024);
			integer(scenario["limits"], "instances", 128);
			integer(scenario["limits"], "actions", 65536);
			integer(scenario["limits"], "objectives", 4096);
			integer(scenario["limits"], "events", 16384);
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
	string(bindings, "objective");
	choice(bindings, "region", map.contains("regions") ? ids(map.at("regions")) : std::vector<std::string>{}, true);
	std::vector<std::string> events;
	if (scenario.contains("events"))
		for (const auto &event : scenario.at("events"))
			events.push_back(event.get<std::string>());
	choice(bindings, "event", events, true);
}
} // namespace phoenix
