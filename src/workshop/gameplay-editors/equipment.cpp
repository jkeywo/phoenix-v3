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
import phoenix.content.catalog;
import phoenix.content.documents;
import phoenix.workshop.authoring;
import phoenix.workshop.tree;
import phoenix.workshop.test_client;

namespace phoenix
{
#include "state.hpp"
Json *GameplayEditors::State::select_entity(WorkshopDocument &document)
{
	if (!document.definitions().contains("entities"))
	{
		text("Create an entity definition first.");
		return nullptr;
	}
	auto &entities = document.definitions()["entities"];
	for (auto &row : entities)
		if (row.at("id") == entity)
			return &row;
	text("Select a local definition to edit. Inherited library definitions remain read-only until overridden.");
	return nullptr;
}

std::vector<std::string> GameplayEditors::State::fitted_ids(const WorkshopDocument &document, const Json &ship)
{
	composition = Json::object();
	composition_catalog = Catalog{};
	auto fallback = ship.contains("systems") ? ids(ship.at("systems")) : std::vector<std::string>{};
	try
	{
		composition_catalog = document.preview_equipment();
		const auto &expanded = composition_catalog.entities.at(ship.at("id").get<std::string>());
		composition = expanded;
		if (expanded.contains("systems"))
			return ids(expanded.at("systems"));
	}
	catch (const std::exception &error)
	{
		ImGui::TextWrapped("%s", error.what());
	}
	return fallback;
}

void GameplayEditors::State::system_details(Json &spec, const std::string &fallback_kind)
{
	const auto kind = spec.value("kind", fallback_kind);
	for (const char *key : {"power_store", "generator", "power_group", "conduit", "cooling_link", "coolant_source"})
		optional_string(spec, key);
	if (kind != "weapon")
	{
		integer(spec, "energy_per_second");
		integer(spec, "discrete_energy");
	}
	boolean(spec, "powerless_start");
	records(spec, "reporters", {{"id", "reporter"}, {"field", "heat"}, {"above", 0}, {"message", ""}},
	        [&](Json &report)
	        {
		        string(report, "id");
		        string(report, "field");
		        integer(report, "above");
		        string(report, "message");
		        optional_string(report, "recovery_message");
		        integer(report, "debounce_ticks");
		        integer(report, "recovery_ticks");
		        integer(report, "minimum_gap_ticks");
		        text("Physical reporters emit measured facts under both human and AI operation.");
	        });
	if (kind == "weapon" || kind == "radar")
	{
		if (spec.contains("directed"))
		{
			ScopedId scope("directed-capability");
			heading("Directed capability");
			auto &directed = spec["directed"];
			strings(directed, "modes");
			strings(directed, "targeting");
			optional_string(directed, "initial_mode");
			optional_string(directed, "initial_targeting");
			text("One operating mode and one targeting policy are selected independently. Concurrent operations "
			     "use separate capabilities and mounts.");
			if (button("Remove directed capability"))
				spec.erase("directed");
		}
		else if (button("Add directed capability"))
			spec["directed"] = {{"modes", Json::array()}, {"targeting", Json::array()}};
	}
	if (kind == "ftl")
	{
		boolean(spec, "enabled");
		integer(spec, "charge_per_second", 100);
		integer(spec, "charging_loss_per_second", 20);
		integer(spec, "ready_loss_per_second", 50);
		integer(spec, "off_discharge_per_second", 80);
		text("FTL uses a dedicated empty reserve. Charge and loss rates compete for actual reactor flow.");
	}
	if (kind == "displacement" || kind == "transport")
	{
		integer(spec, "range_mm", 100000);
		integer(spec, "capacity", 8);
		integer(spec, "transfer_ticks", 40);
		choice(spec, "mode", {"displacement", "shuttle", "boarding_torpedo"});
		if (spec.value("mode", std::string{"displacement"}) != "displacement")
		{
			string(spec, "craft");
			integer(spec, "count", 1);
			integer(spec, "arrival_distance_mm", 5000);
			integer(spec, "arrival_speed_mm_s", 1000);
			integer(spec, "cruise_speed_mm_s", 10000);
		}
	}
	if (kind == "psychic")
	{
		integer(spec, "range_mm", 500000);
		integer(spec, "arc_mdeg", 90000);
		optional_string(spec, "planning_controller");
		strings(spec, "supported_abilities");
		records(spec, "seats", {{"duty", ""}, {"console", ""}, {"controller", ""}, {"enabled", ""}, {"target", ""}},
		        [&](Json &s)
		        {
			        for (const auto *k : {"duty", "console", "controller", "enabled", "target"})
				        string(s, k);
			        optional_string(s, "ability");
		        });
	}
	if (kind == "shuttle_bay")
	{
		integer(spec, "slots", 4);
		integer(spec, "transfer_ticks", 20);
		integer(spec, "docking_range_mm", 10000);
		integer(spec, "docking_speed_mm_s", 5000);
		integer(spec, "flight_spacing_mm", 0);
		integer(spec, "charge_per_second", 0);
		strings(spec, "initial");
		records(spec, "ammunition", {{"source", ""}, {"destination", ""}, {"rounds_per_second", 1}},
		        [&](Json &v)
		        {
			        string(v, "source");
			        string(v, "destination");
			        integer(v, "rounds_per_second", 1);
		        });
	}

	if (kind == "comms")
		integer(spec, "range_mm", 1000000);
	optional_string(spec, "compartment");
	strings(spec, "abilities");
	if (kind == "weapon" || kind == "turret" || kind == "shield")
	{
		integer(spec, "arc_mdeg", 360000);
		integer(spec, "bearing_mdeg");
		integer(spec, "x_mm");
		integer(spec, "y_mm");
	}
	records(spec, "faults",
	        {{"id", "fault"},
	         {"symptom", "Erratic equipment response"},
	         {"diagnosis", "Damaged mechanism requires repair"},
	         {"chance_permille", 100},
	         {"repair_work", 100}},
	        [&](Json &f)
	        {
		        string(f, "id");
		        string(f, "symptom");
		        string(f, "diagnosis");
		        choice(f, "effect", {"output", "disabled", "feed"});
		        integer(f, "chance_permille", 100);
		        integer(f, "damage_threshold", 1);
		        integer(f, "severity", 1);
		        integer(f, "effectiveness_permille", 500);
		        integer(f, "repair_work", 100);
		        boolean(f, "destroyed");
	        });
	if (kind == "weapon")
	{
		optional_string(spec, "magazine");
		strings(spec, "magazines");
		optional_string(spec, "turret");
		optional_string(spec, "muzzle_marker");
		strings(spec, "intercept_classes");
		text("Weapon timing, costs, range and payloads are authored on its directed operating modes.");
	}
	if (kind == "shield")
	{
		string(spec, "shield_effect");
	}
	if (kind == "turret")
	{
		integer(spec, "traverse_mdeg_s", 45000);
		string(spec, "marker");
		string(spec, "visual");
		integer(spec, "scale_permille", 1000);
		boolean(spec, "flip_y");
	}
	if (kind == "magazine")
	{
		if (!spec.contains("stock"))
			spec["stock"] = {{"capacity_tag", "ammo"}, {"capacity", 10}, {"single_resource", true}};
		{
			ScopedId scope("magazine-stock");
			stock_fields(spec["stock"]);
		}
		if (spec.contains("processor"))
		{
			ScopedId scope("magazine-processor");
			processor_fields(spec["processor"]);
			if (button("Remove magazine processor"))
				spec.erase("processor");
		}
		else if (button("Add magazine processor"))
			spec["processor"] = Json::object();
	}
	if (kind == "repair_team" || kind == "team")
	{
		integer(spec, "hp_per_second", 10);
		integer(spec, "stack_penalty_permille", 500);
		optional_string(spec, "type");
		strings(spec, "capabilities");
		integer(spec, "treatment_ticks", 400);
		integer(spec, "stabilise_ticks", 200);
		integer(spec, "injured_effectiveness_permille", 500);
		records(spec, "orders",
		        {{"id", "order"}, {"label", "Team order"}, {"icon", "team"}, {"target", "compartments"}, {"tree", ""}},
		        [&](Json &o)
		        {
			        string(o, "id");
			        string(o, "label");
			        string(o, "icon");
			        choice(o, "target", {"compartments", "damage", "patients"});
			        optional_string(o, "capability");
			        strings(o, "patient_states");
			        string(o, "tree");
		        });
	}

	if (kind == "engine" || ((kind == "boost" || kind == "impulse") && !spec.contains("targets")))
	{
		integer(spec, "max_speed_mm_s");
		integer(spec, "acceleration_mm_s2");
		integer(spec, "deceleration_mm_s2", spec.value("acceleration_mm_s2", std::int64_t{0}));
	}
	if (kind == "engine" || kind == "steering")
		integer(spec, "turn_mdeg_s");
	if (kind == "radar")
	{
		integer(spec, "range_mm");
		bool long_range = spec.contains("long_range_mm");
		if (ImGui::Checkbox(label("Long-range detection").c_str(), &long_range))
		{
			if (long_range)
				spec["long_range_mm"] = spec.value("range_mm", 100000LL) * 3;
			else
				spec.erase("long_range_mm");
		}
		if (long_range)
			integer(spec, "long_range_mm");
		integer(spec, "arc_mdeg", 360000);
		integer(spec, "bearing_mdeg");
		optional_string(spec, "turret");
		text("Focused acquisition capabilities are authored on the directed scan mode.");
		strings(spec, "shows");
	}
	if (kind == "engine")
	{
		integer(spec, "reverse_speed_mm_s");
		integer(spec, "match_stale_ticks", 100);
	}
	if (kind == "engine" || kind == "lateral")
	{
		integer(spec, "lateral_speed_mm_s");
		integer(spec, "lateral_acceleration_mm_s2");
		boolean(spec, "assist_lateral");
		choice(spec, "lateral_direction", {"both", "port", "starboard"});
	}
	if (kind == "engine" || kind == "steering" || kind == "lateral")
	{
		integer(spec, "power_rate");
		integer(spec, "heat_rate");
	}
	auto mode = [&](Json &definition, bool impulse)
	{
		integer(definition, "acceleration_multiplier_permille", 1000);
		integer(definition, "speed_multiplier_permille", 1000);
		integer(definition, "power_rate");
		integer(definition, "heat_rate");
		if (impulse)
		{
			integer(definition, "charge_ticks", 20);
			integer(definition, "steering_permille");
			integer(definition, "charge_power_rate");
			integer(definition, "charge_energy");
			integer(definition, "minimum_reserve");
		}
	};
	if (kind == "boost" || kind == "impulse")
	{
		if (!spec.contains("targets"))
			mode(spec, kind == "impulse");
		else
		{
			integer(spec, "power_rate");
			integer(spec, "heat_rate");
			if (kind == "impulse")
			{
				integer(spec, "charge_ticks", 20);
				integer(spec, "charge_energy");
				integer(spec, "minimum_reserve");
				integer(spec, "steering_permille");
			}
		}
		if (ScopedTree section(label("Modifier targets").c_str()); section)
		{
			if (spec.contains("targets"))
				structure(spec["targets"], true);
			if (button("Add modifier target"))
			{
				if (!spec.contains("targets"))
					spec["targets"] = Json::array();
				spec["targets"].push_back(
				    {{"system", ""}, {"property", "max_speed_mm_s"}, {"multiplier_permille", 1000}});
			}
		}
	}
	if (kind == "engine")
		for (const char *name : {"boost", "impulse"})
		{
			ScopedId identity(name);
			if (ScopedTree section(label(name).c_str()); section)
			{
				if (spec.contains(name))
				{
					mode(spec[name], std::string(name) == "impulse");
					if (button("Remove mode"))
						spec.erase(name);
				}
				else if (button("Add mode"))
					spec[name] = Json::object();
			}
		}
}

void GameplayEditors::State::fitted(WorkshopDocument &document)
{
	auto *selected = select_entity(document);
	if (!selected)
		return;
	auto &ship = *selected;
	personnel(ship, document);
	{
		ScopedTree panel("Host audio");
		if (panel)
		{
			if (!ship.contains("audio"))
				ship["audio"] = Json::object();
			audio_fields(ship["audio"], "ambient");
			audio_fields(ship["audio"], "engine");
		}
	}
	{
		ScopedTree panel("Storage and logistics");
		if (panel)
		{
			if (!ship.contains("logistics") && button("Configure material stores"))
				ship["logistics"] = {{"controller", ""}, {"stores", Json::array()}, {"consumers", Json::array()}};
			if (ship.contains("logistics"))
			{
				auto &l = ship["logistics"];
				string(l, "controller");
				integer(l, "units_per_second", 20);
				integer(l, "concurrency", 4);
				integer(l, "batch_size", 10);
				records(l, "stores",
				        {{"id", "hold"},
				         {"compartment", ""},
				         {"stock", {{"capacity_tag", "cargo"}, {"capacity", 100}}},
				         {"targets", Json::object()}},
				        [&](Json &m)
				        {
					        string(m, "id");
					        string(m, "compartment");
					        optional_string(m, "controller");
					        optional_string(m, "system");
					        stock_fields(m["stock"]);
					        boolean(m, "main", false);
					        structured_field(m, "targets");
				        });
				records(l, "consumers", {{"system", ""}, {"store", ""}, {"costs", Json::object()}},
				        [&](Json &m)
				        {
					        string(m, "system");
					        string(m, "store");
					        structured_field(m, "costs");
				        });
			}
		}
	}
	{
		ScopedTree panel("Docking ports");
		if (panel)
		{
			if (!ship.contains("docking") && button("Configure docking ports"))
				ship["docking"] = {{"ports", Json::array()}};
			if (ship.contains("docking"))
				records(ship["docking"], "ports",
				        {{"id", "port"}, {"controller", ""}, {"compartment", ""}, {"compatibility", "standard"}},
				        [&](Json &m)
				        {
					        string(m, "id");
					        string(m, "controller");
					        string(m, "compartment");
					        string(m, "compatibility");
					        integer(m, "x_mm", 0);
					        integer(m, "y_mm", 0);
					        integer(m, "z_mm", 0);
					        integer(m, "heading_mdeg", 0);
					        integer(m, "capture_range_mm", 1000);
					        integer(m, "capture_speed_mm_s", 500);
					        integer(m, "alignment_mdeg", 5000);
					        integer(m, "mating_ticks", 20);
					        integer(m, "separation_ticks", 20);
					        integer(m, "units_per_second", 20);
					        boolean(m, "auto_accept_allies", false);
					        boolean(m, "supply", false);
					        boolean(m, "emergency_release", false);
					        integer(m, "emergency_damage", 0);
				        });
		}
	}

	{
		ScopedTree panel("Ability control bindings");
		if (panel)
		{
			if (!ship.contains("ability_controls"))
				ship["ability_controls"] = Json::object();
			auto &controls = ship["ability_controls"];
			std::string remove;
			for (auto &[id, binding] : controls.items())
			{
				ScopedTree endpoint(id.c_str());
				if (!endpoint)
					continue;
				choice(binding, "source", {"officer", "duty", "equipment"});
				string(binding, "owner");
				choice(binding, "ability", document.definition_choices(WorkshopDefinitionKind::Effect));
				for (const auto *key : {"console", "controller", "enabled"})
					string(binding, key);
				for (const auto *key : {"target", "apparatus", "planner"})
					optional_string(binding, key);
				if (button("Remove ability endpoint"))
					remove = id;
			}
			if (!remove.empty())
				controls.erase(remove);
			if (button("Add ability endpoint"))
				controls[fresh(controls, "ability")] = {{"source", "equipment"}, {"owner", ""},      {"ability", ""},
				                                        {"console", ""},         {"controller", ""}, {"enabled", ""}};
		}
	}

	{
		ScopedTree panel("Process control bindings");
		if (panel)
		{
			if (!ship.contains("process_controls"))
				ship["process_controls"] = Json::object();
			auto &controls = ship["process_controls"];
			object_rows(controls,
			            [&](const std::string &id, Json &binding)
			            {
				            ScopedId identity(id.c_str());
				            ImGui::TextUnformatted(id.c_str());
				            choice(binding, "source", {"entity", "equipment", "officer", "compartment"});
				            string(binding, "owner");
				            string(binding, "process");
				            for (const auto *field : {"controller", "enabled"})
					            string(binding, field);
				            return button("Remove process endpoint");
			            });
			if (button("Add process endpoint"))
				controls[fresh(controls, "process")] = {
				    {"source", "equipment"}, {"owner", ""}, {"process", ""}, {"controller", ""}, {"enabled", ""}};
		}
	}

	{
		ScopedTree panel("Boarding missions");
		if (panel)
			records(ship, "boarding_missions",
			        {{"id", "boarding"},
			         {"title_key", "Board target"},
			         {"entry", ""},
			         {"tree", ""},
			         {"dialogue", ""},
			         {"slots", Json::array()},
			         {"encounters", Json::array()},
			         {"objectives", Json::object()}},
			        [&](Json &m)
			        {
				        string(m, "id");
				        string(m, "title_key");
				        string(m, "entry");
				        choice(m, "tree", document.definition_choices(WorkshopDefinitionKind::Tree));
				        choice(m, "dialogue", document.definition_choices(WorkshopDefinitionKind::Tree));
				        boolean(m, "retain_craft", true);
				        strings(m, "docking_ports");
				        records(m, "slots",
				                {{"id", "security"}, {"kind", "team"}, {"capability", "security"}, {"required", true}},
				                [&](Json &slot)
				                {
					                string(slot, "id");
					                choice(slot, "kind", {"team", "officer"});
					                optional_string(slot, "capability");
					                optional_string(slot, "team_type");
					                optional_string(slot, "trait");
					                boolean(slot, "required", true);
				                });
				        records(m, "encounters", {{"id", "guards"}, {"compartment", ""}, {"defenders", Json::array()}},
				                [&](Json &e)
				                {
					                string(e, "id");
					                string(e, "compartment");
					                strings(e, "defenders");
					                optional_string(e, "report");
					                integer(e, "success_permille", 700);
					                integer(e, "team_bonus_permille", 100);
					                integer(e, "defender_penalty_permille", 150);
					                integer(e, "injury_permille", 250);
					                integer(e, "loss_permille", 50);
					                integer(e, "medical_reduction_permille", 150);
					                integer(e, "officer_injury_permille", 0);
					                integer(e, "officer_loss_permille", 0);
					                integer(e, "assessment_ticks", 20);
					                structured_field(e, "defences");
				                });
				        structured_field(m, "objectives");
			        });
	}
	{
		ScopedTree panel("Craft and transport");
		if (panel)
		{
			if (!ship.contains("craft") && button("Add craft configuration"))
				ship["craft"] = {{"role", "shuttle"}};
			if (ship.contains("craft"))
			{
				auto &c = ship["craft"];
				choice(c, "role", {"shuttle", "interceptor", "bomber", "boarding_torpedo"});
				integer(c, "vertical_speed_mm_s", 10000);
				integer(c, "vertical_acceleration_mm_s2", 5000);
				integer(c, "cruise_altitude_mm", 15000);
				integer(c, "engagement_range_mm", 200000);
				integer(c, "return_reserve_permille", 200);
			}
			if (!ship.contains("transport") && button("Configure transport capabilities"))
				ship["transport"] = {
				    {"capacity", 64}, {"capabilities", Json::array()}, {"capability_systems", Json::object()}};
			if (ship.contains("transport"))
			{
				auto &t = ship["transport"];
				integer(t, "capacity", 64);
				optional_string(t, "compartment");
				strings(t, "capabilities");
				for (const auto &tag : t.value("capabilities", Json::array()))
				{
					if (!t.contains("capability_systems"))
						t["capability_systems"] = Json::object();
					strings(t["capability_systems"], tag.get<std::string>().c_str());
				}
			}
		}
	}
	{
		ScopedTree panel("External Comms");
		if (panel)
		{
			if (!ship.contains("comms") && button("Add Comms directory"))
				ship["comms"] = {{"characters", Json::array()}};
			if (ship.contains("comms"))
			{
				choice(ship["comms"], "default_conversation", document.definition_choices(WorkshopDefinitionKind::Tree),
				       true);
				records(ship["comms"], "characters",
				        {{"id", "character"}, {"name_key", "New character"}, {"public", true}},
				        [&](Json &p)
				        {
					        string(p, "id");
					        string(p, "name_key");
					        optional_string(p, "portrait");
					        boolean(p, "public", true);
					        choice(p, "default_conversation", document.definition_choices(WorkshopDefinitionKind::Tree),
					               true);
				        });
			}
		}
	}
	{
		ScopedTree panel("Alert conditions and Viewscreen");
		if (panel)
		{
			if (!ship.contains("bridge") && button("Add bridge configuration"))
				ship["bridge"] = {{"conditions", Json::array()}};
			if (ship.contains("bridge"))
			{
				auto &bridge = ship["bridge"];
				optional_string(bridge, "alert_console");
				optional_string(bridge, "sidebar_default");
				optional_string(bridge, "initial");
				records(bridge, "conditions",
				        {{"id", "condition"}, {"label", "Condition"}, {"colour", "#e73535"}, {"glow", false}},
				        [&](Json &condition)
				        {
					        string(condition, "id");
					        string(condition, "label");
					        optional_string(condition, "description");
					        string(condition, "colour");
					        boolean(condition, "glow", false);
					        optional_string(condition, "siren");
					        optional_string(condition, "music");
					        edit_field(label("music_gain").c_str(), condition, "music_gain", 1.0);
				        });
			}
		}
	}
	strings(ship, "tags");
	choice(ship, "extends", document.definition_choices(WorkshopDefinitionKind::Entity), true);
	if (system_entity != entity)
	{
		selected_system.clear();
		system_entity = entity;
	}
	heading("Fitted Systems");
	static std::string new_system;
	ImGui::InputText(label("New System identity").c_str(), &new_system);
	if (button("Add System") && !new_system.empty())
	{
		if (!ship.contains("systems"))
			ship["systems"] = Json::object();
		if (!ship["systems"].contains(new_system))
			ship["systems"][new_system] = {{"kind", "engine"}, {"max_speed_mm_s", 10000}, {"acceleration_mm_s2", 1000}};
		selected_system = new_system;
		new_system.clear();
	}
	std::string remove;
	if (ship.contains("systems"))
	{
		for (auto it = ship["systems"].begin(); it != ship["systems"].end(); ++it)
			if (ImGui::Selectable(it.key().c_str(), selected_system == it.key()))
				selected_system = it.key();
		if (ship["systems"].contains(selected_system))
		{
			ScopedId identity(selected_system.c_str());
			auto &spec = ship["systems"][selected_system];
			heading("Selected System");
			ImGui::TextUnformatted(selected_system.c_str());
			choice(spec, "template", document.definition_choices(WorkshopDefinitionKind::Equipment), true);
			choice(spec, "kind", {"engine",    "steering", "radar",       "generator", "distribution", "coolant",
			                      "storage",   "lateral",  "boost",       "impulse",   "weapon",       "turret",
			                      "magazine",  "team",     "repair_team", "shield",    "comms",        "displacement",
			                      "transport", "dispatch", "ftl",         "psychic"},
			       true);
			auto kind = equipment_kind(document, spec);
			if (kind.empty())
				kind = selected_system;
			system_details(spec, kind);
			rule_bindings_fields(spec, document);
			owner_properties_fields(spec, document);
			telemetry_records(spec, document);
			scalar_map(spec, "parameters");
			if (button("Remove System"))
				remove = selected_system;
		}
	}
	if (!remove.empty())
		ship["systems"].erase(remove);
	const auto systems = fitted_ids(document, ship);
	if (composition.contains("systems"))
	{
		if (ScopedTree section(label("Inherited and assembled equipment").c_str()); section)
		{
			for (auto it = composition["systems"].begin(); it != composition["systems"].end(); ++it)
				if (!ship.contains("systems") || !ship.at("systems").contains(it.key()))
				{
					ScopedId identity(it.key().c_str());
					ImGui::TextUnformatted(it.key().c_str());
					structure(it.value(), false);
				}
		}
	}
	if (!ship.contains("stations") && composition.contains("stations"))
	{
		text("Stations are inherited. Copy them only when authoring an explicit local ownership override.");
		if (button("Override inherited Stations"))
			ship["stations"] = composition.at("stations");
	}
	records(ship, "stations", {{"id", "station"}, {"consoles", Json::array()}},
	        [&](Json &station)
	        {
		        string(station, "id");
		        records(station, "consoles",
		                {{"id", "console"}, {"definition", ""}, {"systems", Json::array()}, {"feeds", Json::array()}},
		                [&](Json &console)
		                {
			                string(console, "id");
			                choice(console, "definition", document.definition_choices(WorkshopDefinitionKind::Console));
			                names(console, "systems", systems);
			                names(console, "feeds", systems);
			                boolean(console, "shield_feed");
			                names(console, "human_preferences", ids(ship.at("stations")));
			                gameplay_panel(console);
		                });
	        });
}

void GameplayEditors::State::placements(WorkshopDocument &document)
{
	if (auto *ship = select_entity(document))
	{
		names(*ship, "message_templates", document.definition_choices(WorkshopDefinitionKind::Message));
		structured_field(*ship, "parameters");
		services(*ship);
		records(*ship, "assemblies",
		        {{"id", "placement"}, {"assembly", ""}, {"bindings", Json::object()}, {"overrides", Json::object()}},
		        [&](Json &placement)
		        {
			        string(placement, "id");
			        choice(placement, "assembly", document.definition_choices(WorkshopDefinitionKind::Assembly));
			        scalar_map(placement, "bindings");
			        structured_field(placement, "overrides");
		        });
	}
}

void GameplayEditors::State::services(Json &parent)
{
	heading("External service endpoints");
	static std::map<ImGuiID, std::string> names;
	const auto identity = ImGui::GetID("service-name");
	ImGui::InputText(label("Service name").c_str(), &names[identity]);
	if (button("Add endpoint") && !names[identity].empty())
	{
		if (!parent.contains("services"))
			parent["services"] = Json::object();
		if (!parent["services"].contains(names[identity]))
			parent["services"][names[identity]] = {{"kind", "power"}, {"direction", "output"}, {"target", ""}};
	}
	if (parent.contains("services"))
		object_rows(parent["services"],
		            [&](const std::string &name, Json &row)
		            {
			            ImGui::TextUnformatted(name.c_str());
			            choice(row, "kind", {"power", "coolant", "information"});
			            choice(row, "direction", {"input", "output"});
			            string(row, "target");
			            return button("Remove endpoint");
		            });
}

std::string GameplayEditors::State::equipment_kind(const WorkshopDocument &document, const Json &instance) const
{
	if (instance.contains("kind"))
		return instance.at("kind").get<std::string>();
	const auto name = instance.value("template", instance.value("extends", std::string{}));
	return name.empty()
	           ? std::string{}
	           : document.preview_definition(WorkshopDefinitionKind::Equipment, name).value("kind", std::string{});
}
} // namespace phoenix
