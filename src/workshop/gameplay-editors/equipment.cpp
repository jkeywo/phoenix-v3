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
	auto fallback = ship.contains("systems") ? ids(ship.at("systems")) : std::vector<std::string>{};
	if (composition_revision != document.revision())
	{
		composition_revision = document.revision();
		composition_error.clear();
		composition_catalog = Catalog{};
		try
		{
			composition_catalog = document.preview_equipment();
		}
		catch (const std::exception &error)
		{
			composition_error = error.what();
		}
	}
	if (!composition_error.empty())
		ImGui::TextWrapped("%s", composition_error.c_str());
	else if (const auto found = composition_catalog.entities.find(ship.at("id").get<std::string>());
	         found != composition_catalog.entities.end())
	{
		composition = found->second;
		if (composition.contains("systems"))
			return ids(composition.at("systems"));
	}
	return fallback;
}

void GameplayEditors::State::system_details(WorkshopDocument &document, Json &spec, const std::string &fallback_kind)
{
	const auto kind = spec.value("kind", fallback_kind);
	for (const char *key : {"power_store", "generator", "power_group", "conduit", "cooling_link", "coolant_source"})
		optional_string(spec, key);
	if (kind != "weapon")
	{
		edit_registered(spec, "gameplay-editors.equipment.energy_per_second");
		edit_registered(spec, "gameplay-editors.equipment.discrete_energy");
	}
	edit_registered(spec, "gameplay-editors.equipment.powerless_start");
	records(spec, "reporters", workshop_record("gameplay-editors.equipment.reporters"),
	        [&](Json &report)
	        {
		        edit_registered(report, "gameplay-editors.equipment.id");
		        edit_registered(report, "gameplay-editors.equipment.field");
		        edit_registered(report, "gameplay-editors.equipment.above");
		        edit_registered(report, "gameplay-editors.equipment.message");
		        optional_string(report, "recovery_message");
		        edit_registered(report, "gameplay-editors.equipment.debounce_ticks");
		        edit_registered(report, "gameplay-editors.equipment.recovery_ticks");
		        edit_registered(report, "gameplay-editors.equipment.minimum_gap_ticks");
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
			spec["directed"] = workshop_seed("gameplay-editors.equipment.directed");
	}
	if (kind == "ftl")
	{
		edit_registered(spec, "gameplay-editors.equipment.enabled");
		edit_registered(spec, "gameplay-editors.equipment.charge_per_second");
		edit_registered(spec, "gameplay-editors.equipment.charging_loss_per_second");
		edit_registered(spec, "gameplay-editors.equipment.ready_loss_per_second");
		edit_registered(spec, "gameplay-editors.equipment.off_discharge_per_second");
		text("FTL uses a dedicated empty reserve. Charge and loss rates compete for actual reactor flow.");
	}
	if (kind == "displacement" || kind == "transport")
	{
		edit_registered(spec, "gameplay-editors.equipment.range_mm");
		edit_registered(spec, "gameplay-editors.equipment.capacity");
		edit_registered(spec, "gameplay-editors.equipment.transfer_ticks");
		edit_registered(spec, "gameplay-editors.equipment.mode");
		if (spec.value("mode", std::string{"displacement"}) != "displacement")
		{
			edit_registered(spec, "gameplay-editors.equipment.craft");
			edit_registered(spec, "gameplay-editors.equipment.count");
			edit_registered(spec, "gameplay-editors.equipment.arrival_distance_mm");
			edit_registered(spec, "gameplay-editors.equipment.arrival_speed_mm_s");
			edit_registered(spec, "gameplay-editors.equipment.cruise_speed_mm_s");
		}
	}
	if (kind == "psychic")
	{
		edit_registered(spec, "gameplay-editors.equipment.range_mm.2");
		edit_registered(spec, "gameplay-editors.equipment.arc_mdeg");
		optional_string(spec, "planning_controller");
		strings(spec, "supported_abilities");
		records(spec, "seats", workshop_record("gameplay-editors.equipment.seats"),
		        [&](Json &s)
		        {
			        edit_shared_group(s, "gameplay-editors.equipment.group.duty");
			        optional_string(s, "ability");
		        });
	}
	if (kind == "shuttle_bay")
	{
		edit_registered(spec, "gameplay-editors.equipment.slots");
		edit_registered(spec, "gameplay-editors.equipment.transfer_ticks.2");
		edit_registered(spec, "gameplay-editors.equipment.docking_range_mm");
		edit_registered(spec, "gameplay-editors.equipment.docking_speed_mm_s");
		edit_registered(spec, "gameplay-editors.equipment.flight_spacing_mm");
		edit_registered(spec, "gameplay-editors.equipment.charge_per_second.2");
		strings(spec, "initial");
		records(spec, "ammunition", workshop_record("gameplay-editors.equipment.ammunition"),
		        [&](Json &v)
		        {
			        edit_registered(v, "gameplay-editors.equipment.source");
			        edit_registered(v, "gameplay-editors.equipment.destination");
			        edit_registered(v, "gameplay-editors.equipment.rounds_per_second");
		        });
	}

	if (kind == "comms")
		edit_registered(spec, "gameplay-editors.equipment.range_mm.3");
	optional_string(spec, "compartment");
	strings(spec, "abilities");
	if (kind == "weapon" || kind == "turret" || kind == "shield")
	{
		edit_registered(spec, "gameplay-editors.equipment.arc_mdeg.2");
		edit_registered(spec, "gameplay-editors.equipment.bearing_mdeg");
		edit_registered(spec, "gameplay-editors.equipment.x_mm");
		edit_registered(spec, "gameplay-editors.equipment.y_mm");
	}
	records(spec, "faults", workshop_record("gameplay-editors.equipment.faults"),
	        [&](Json &f)
	        {
		        edit_registered(f, "gameplay-editors.equipment.id");
		        edit_registered(f, "gameplay-editors.equipment.symptom");
		        edit_registered(f, "gameplay-editors.equipment.diagnosis");
		        edit_registered(f, "gameplay-editors.equipment.effect");
		        edit_registered(f, "gameplay-editors.equipment.chance_permille");
		        edit_registered(f, "gameplay-editors.equipment.damage_threshold");
		        edit_registered(f, "gameplay-editors.equipment.severity");
		        edit_registered(f, "gameplay-editors.equipment.effectiveness_permille");
		        edit_registered(f, "gameplay-editors.equipment.repair_work");
		        edit_registered(f, "gameplay-editors.equipment.destroyed");
	        });
	if (kind == "weapon")
	{
		edit_registered(spec, "gameplay-editors.equipment.radar_coverage");
		optional_string(spec, "coverage_label");
		edit_registered(spec, "gameplay-editors.equipment.coverage_when_enabled");
		optional_string(spec, "magazine");
		strings(spec, "magazines");
		optional_string(spec, "turret");
		optional_string(spec, "muzzle_marker");
		strings(spec, "intercept_classes");
		text("Weapon timing, costs, range and payloads are authored on its directed operating modes.");
	}
	if (kind == "shield")
	{
		edit_registered(spec, "gameplay-editors.equipment.shield_effect");
	}
	if (kind == "turret")
	{
		edit_registered(spec, "gameplay-editors.equipment.traverse_mdeg_s");
		edit_registered(spec, "gameplay-editors.equipment.marker");
		edit_registered(spec, "gameplay-editors.equipment.visual");
		edit_registered(spec, "gameplay-editors.equipment.scale_permille");
		edit_registered(spec, "gameplay-editors.equipment.flip_y");
	}
	if (kind == "magazine")
	{
		if (!spec.contains("stock"))
			spec["stock"] = workshop_seed("gameplay-editors.equipment.stock");
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
			spec["processor"] = workshop_seed("gameplay-editors.equipment.processor");
	}
	if (kind == "repair_team" || kind == "team")
	{
		edit_registered(spec, "gameplay-editors.equipment.home_compartment");
		edit_registered(spec, "gameplay-editors.equipment.hp_per_second");
		edit_registered(spec, "gameplay-editors.equipment.stack_penalty_permille");
		optional_string(spec, "type");
		strings(spec, "capabilities");
		edit_registered(spec, "gameplay-editors.equipment.treatment_ticks");
		edit_registered(spec, "gameplay-editors.equipment.stabilise_ticks");
		edit_registered(spec, "gameplay-editors.equipment.injured_effectiveness_permille");
		records(spec, "orders", workshop_record("gameplay-editors.equipment.orders"),
		        [&](Json &o)
		        {
			        edit_registered(o, "gameplay-editors.equipment.id");
			        edit_registered(o, "gameplay-editors.equipment.label");
			        edit_registered(o, "gameplay-editors.equipment.icon");
			        edit_registered(o, "gameplay-editors.equipment.target");
			        optional_string(o, "capability");
			        strings(o, "patient_states");
			        tree_editor.tree_binding(document, o, "tree", "team", false);
		        });
	}

	if (kind == "engine" || ((kind == "boost" || kind == "impulse") && !spec.contains("targets")))
	{
		edit_registered(spec, "gameplay-editors.equipment.max_speed_mm_s");
		edit_registered(spec, "gameplay-editors.equipment.acceleration_mm_s2");
		edit_shared_field(label("deceleration_mm_s2").c_str(), spec, "equipment.deceleration");
	}
	if (kind == "engine" || kind == "steering")
		edit_registered(spec, "gameplay-editors.equipment.turn_mdeg_s");
	if (kind == "radar")
	{
		edit_registered(spec, "gameplay-editors.equipment.radar_arc");
		edit_registered(spec, "gameplay-editors.equipment.range_mm.4");
		edit_shared_field(label("Long-range detection").c_str(), spec, "radar.long_range");
		const bool long_range = spec.contains("long_range_mm");

		if (long_range)
			edit_registered(spec, "gameplay-editors.equipment.long_range_mm");
		edit_registered(spec, "gameplay-editors.equipment.arc_mdeg.2");
		edit_registered(spec, "gameplay-editors.equipment.bearing_mdeg");
		optional_string(spec, "turret");
		text("Focused acquisition capabilities are authored on the directed scan mode.");
		strings(spec, "shows");
	}
	if (kind == "engine")
	{
		edit_registered(spec, "gameplay-editors.equipment.reverse_speed_mm_s");
		edit_registered(spec, "gameplay-editors.equipment.match_stale_ticks");
	}
	if (kind == "engine" || kind == "lateral")
	{
		edit_registered(spec, "gameplay-editors.equipment.lateral_speed_mm_s");
		edit_registered(spec, "gameplay-editors.equipment.lateral_acceleration_mm_s2");
		edit_registered(spec, "gameplay-editors.equipment.assist_lateral");
		edit_registered(spec, "gameplay-editors.equipment.lateral_direction");
	}
	if (kind == "engine" || kind == "steering" || kind == "lateral")
	{
		edit_registered(spec, "gameplay-editors.equipment.power_rate");
		edit_registered(spec, "gameplay-editors.equipment.heat_rate");
	}
	auto mode = [&](Json &definition, bool impulse)
	{
		edit_registered(definition, "gameplay-editors.equipment.acceleration_multiplier_permille");
		edit_registered(definition, "gameplay-editors.equipment.speed_multiplier_permille");
		edit_registered(definition, "gameplay-editors.equipment.power_rate");
		edit_registered(definition, "gameplay-editors.equipment.heat_rate");
		if (impulse)
		{
			edit_registered(definition, "gameplay-editors.equipment.charge_ticks");
			edit_registered(definition, "gameplay-editors.equipment.steering_permille");
			edit_registered(definition, "gameplay-editors.equipment.charge_power_rate");
			edit_registered(definition, "gameplay-editors.equipment.charge_energy");
			edit_registered(definition, "gameplay-editors.equipment.minimum_reserve");
		}
	};
	if (kind == "boost" || kind == "impulse")
	{
		if (!spec.contains("targets"))
			mode(spec, kind == "impulse");
		else
		{
			edit_registered(spec, "gameplay-editors.equipment.power_rate");
			edit_registered(spec, "gameplay-editors.equipment.heat_rate");
			if (kind == "impulse")
			{
				edit_registered(spec, "gameplay-editors.equipment.charge_ticks");
				edit_registered(spec, "gameplay-editors.equipment.charge_energy");
				edit_registered(spec, "gameplay-editors.equipment.minimum_reserve");
				edit_registered(spec, "gameplay-editors.equipment.steering_permille");
			}
		}
		if (ScopedTree section(label("Modifier targets").c_str()); section)
		{
			if (spec.contains("targets"))
				structure(spec["targets"], true);
			if (button("Add modifier target"))
			{
				if (!spec.contains("targets"))
					spec["targets"] = workshop_seed("gameplay-editors.equipment.targets");
				spec["targets"].push_back(workshop_record("gameplay-editors.equipment.modifiers"));
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
					(void)workshop_custom_action(spec, "field.ensure", {{"field", name}, {"type", "object"}});
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
				ship["audio"] = workshop_seed("gameplay-editors.equipment.audio");
			audio_fields(ship["audio"], "ambient");
			audio_fields(ship["audio"], "engine");
			for (const auto *key : {"ftl_arrival", "ftl_departure", "comms_attention", "critical_warning"})
				audio_fields(ship["audio"], key);
		}
	}
	{
		ScopedTree panel("Storage and logistics");
		if (panel)
		{
			if (!ship.contains("logistics") && button("Configure material stores"))
				ship["logistics"] = workshop_seed("gameplay-editors.equipment.logistics");
			if (ship.contains("logistics"))
			{
				auto &l = ship["logistics"];
				edit_registered(l, "gameplay-editors.equipment.controller");
				edit_registered(l, "gameplay-editors.equipment.units_per_second");
				edit_registered(l, "gameplay-editors.equipment.concurrency");
				edit_registered(l, "gameplay-editors.equipment.batch_size");
				records(l, "stores", workshop_record("gameplay-editors.equipment.stores"),
				        [&](Json &m)
				        {
					        edit_registered(m, "gameplay-editors.equipment.id");
					        edit_registered(m, "gameplay-editors.equipment.compartment");
					        optional_string(m, "controller");
					        optional_string(m, "system");
					        stock_fields(m["stock"]);
					        edit_registered(m, "gameplay-editors.equipment.main");
					        structured_field(m, "targets");
				        });
				records(l, "consumers", workshop_record("gameplay-editors.equipment.consumers"),
				        [&](Json &m)
				        {
					        edit_registered(m, "gameplay-editors.equipment.system");
					        edit_registered(m, "gameplay-editors.equipment.store");
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
				ship["docking"] = workshop_seed("gameplay-editors.equipment.docking");
			if (ship.contains("docking"))
				records(ship["docking"], "ports", workshop_record("gameplay-editors.equipment.ports"),
				        [&](Json &m)
				        {
					        edit_registered(m, "gameplay-editors.equipment.id");
					        edit_registered(m, "gameplay-editors.equipment.controller");
					        edit_registered(m, "gameplay-editors.equipment.compartment");
					        edit_registered(m, "gameplay-editors.equipment.compatibility");
					        edit_registered(m, "gameplay-editors.equipment.x_mm");
					        edit_registered(m, "gameplay-editors.equipment.y_mm");
					        edit_registered(m, "gameplay-editors.equipment.z_mm");
					        edit_registered(m, "gameplay-editors.equipment.heading_mdeg");
					        edit_registered(m, "gameplay-editors.equipment.capture_range_mm");
					        edit_registered(m, "gameplay-editors.equipment.capture_speed_mm_s");
					        edit_registered(m, "gameplay-editors.equipment.alignment_mdeg");
					        edit_registered(m, "gameplay-editors.equipment.mating_ticks");
					        edit_registered(m, "gameplay-editors.equipment.separation_ticks");
					        edit_registered(m, "gameplay-editors.equipment.units_per_second");
					        edit_registered(m, "gameplay-editors.equipment.auto_accept_allies");
					        edit_registered(m, "gameplay-editors.equipment.supply");
					        edit_registered(m, "gameplay-editors.equipment.emergency_release");
					        edit_registered(m, "gameplay-editors.equipment.emergency_damage");
				        });
		}
	}

	{
		ScopedTree panel("Ability control bindings");
		if (panel)
		{
			if (!ship.contains("ability_controls"))
				ship["ability_controls"] = workshop_seed("gameplay-editors.equipment.ability_controls");
			auto &controls = ship["ability_controls"];
			std::string remove;
			for (auto &[id, binding] : controls.items())
			{
				ScopedTree endpoint(id.c_str());
				if (!endpoint)
					continue;
				edit_registered(binding, "gameplay-editors.equipment.source.2");
				edit_registered(binding, "gameplay-editors.equipment.owner");
				choice(binding, "ability", document.definition_choices(WorkshopDefinitionKind::Effect));
				edit_shared_group(binding, "gameplay-editors.equipment.group.console");
				for (const auto *key : {"target", "apparatus", "planner"})
					optional_string(binding, key);
				if (button("Remove ability endpoint"))
					remove = id;
			}
			if (!remove.empty())
				controls.erase(remove);
			if (button("Add ability endpoint"))
				(void)workshop_custom_action(controls, "ability.endpoint");
		}
	}

	{
		ScopedTree panel("Process control bindings");
		if (panel)
		{
			if (!ship.contains("process_controls"))
				ship["process_controls"] = workshop_seed("gameplay-editors.equipment.process_controls");
			auto &controls = ship["process_controls"];
			object_rows(controls,
			            [&](const std::string &id, Json &binding)
			            {
				            ScopedId identity(id.c_str());
				            ImGui::TextUnformatted(id.c_str());
				            edit_registered(binding, "gameplay-editors.equipment.source.3");
				            edit_registered(binding, "gameplay-editors.equipment.owner");
				            edit_registered(binding, "gameplay-editors.equipment.process");
				            edit_shared_group(binding, "gameplay-editors.equipment.group.controller");
				            return button("Remove process endpoint");
			            });
			if (button("Add process endpoint"))
				(void)workshop_custom_action(controls, "process.endpoint");
		}
	}

	{
		ScopedTree panel("Boarding missions");
		if (panel)
			records(ship, "boarding_missions", workshop_record("gameplay-editors.equipment.boarding_missions"),
			        [&](Json &m)
			        {
				        edit_registered(m, "gameplay-editors.equipment.id");
				        edit_registered(m, "gameplay-editors.equipment.title_key");
				        edit_registered(m, "gameplay-editors.equipment.entry");
				        tree_editor.tree_binding(document, m, "tree", "away", false);
				        tree_editor.tree_binding(document, m, "dialogue", "conversation", false);
				        edit_registered(m, "gameplay-editors.equipment.retain_craft");
				        strings(m, "docking_ports");
				        records(m, "slots", workshop_record("gameplay-editors.equipment.slots"),
				                [&](Json &slot)
				                {
					                edit_registered(slot, "gameplay-editors.equipment.id");
					                edit_registered(slot, "gameplay-editors.equipment.kind");
					                optional_string(slot, "capability");
					                optional_string(slot, "team_type");
					                optional_string(slot, "trait");
					                edit_registered(slot, "gameplay-editors.equipment.required");
				                });
				        records(m, "encounters", workshop_record("gameplay-editors.equipment.encounters"),
				                [&](Json &e)
				                {
					                edit_registered(e, "gameplay-editors.equipment.id");
					                edit_registered(e, "gameplay-editors.equipment.compartment");
					                strings(e, "defenders");
					                optional_string(e, "report");
					                edit_registered(e, "gameplay-editors.equipment.success_permille");
					                edit_registered(e, "gameplay-editors.equipment.team_bonus_permille");
					                edit_registered(e, "gameplay-editors.equipment.defender_penalty_permille");
					                edit_registered(e, "gameplay-editors.equipment.injury_permille");
					                edit_registered(e, "gameplay-editors.equipment.loss_permille");
					                edit_registered(e, "gameplay-editors.equipment.medical_reduction_permille");
					                edit_registered(e, "gameplay-editors.equipment.officer_injury_permille");
					                edit_registered(e, "gameplay-editors.equipment.officer_loss_permille");
					                edit_registered(e, "gameplay-editors.equipment.assessment_ticks");
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
				ship["craft"] = workshop_seed("gameplay-editors.equipment.craft");
			if (ship.contains("craft"))
			{
				auto &c = ship["craft"];
				edit_registered(c, "gameplay-editors.equipment.role");
				edit_registered(c, "gameplay-editors.equipment.vertical_speed_mm_s");
				edit_registered(c, "gameplay-editors.equipment.vertical_acceleration_mm_s2");
				edit_registered(c, "gameplay-editors.equipment.cruise_altitude_mm");
				edit_registered(c, "gameplay-editors.equipment.engagement_range_mm");
				edit_registered(c, "gameplay-editors.equipment.return_reserve_permille");
			}
			if (!ship.contains("transport") && button("Configure transport capabilities"))
				ship["transport"] = workshop_seed("gameplay-editors.equipment.transport");
			if (ship.contains("transport"))
			{
				auto &t = ship["transport"];
				edit_registered(t, "gameplay-editors.equipment.capacity.2");
				optional_string(t, "compartment");
				strings(t, "capabilities");
				for (const auto &tag : t.value("capabilities", Json::array()))
				{
					if (!t.contains("capability_systems"))
						t["capability_systems"] = workshop_seed("gameplay-editors.equipment.capability_systems");
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
				ship["comms"] = workshop_seed("gameplay-editors.equipment.comms");
			if (ship.contains("comms"))
			{
				tree_editor.tree_binding(document, ship["comms"], "default_conversation", "conversation", true);
				records(ship["comms"], "characters", workshop_record("gameplay-editors.equipment.characters"),
				        [&](Json &p)
				        {
					        edit_registered(p, "gameplay-editors.equipment.id");
					        edit_registered(p, "gameplay-editors.equipment.name_key");
					        optional_string(p, "portrait");
					        edit_registered(p, "gameplay-editors.equipment.public");
					        tree_editor.tree_binding(document, p, "default_conversation", "conversation", true);
				        });
			}
		}
	}
	{
		ScopedTree panel("Alert conditions and Viewscreen");
		if (panel)
		{
			if (!ship.contains("bridge") && button("Add bridge configuration"))
				ship["bridge"] = workshop_seed("gameplay-editors.equipment.bridge");
			if (ship.contains("bridge"))
			{
				auto &bridge = ship["bridge"];
				optional_string(bridge, "alert_console");
				optional_string(bridge, "sidebar_default");
				optional_string(bridge, "initial");
				records(bridge, "conditions", workshop_record("gameplay-editors.equipment.conditions"),
				        [&](Json &condition)
				        {
					        edit_registered(condition, "gameplay-editors.equipment.id");
					        edit_registered(condition, "gameplay-editors.equipment.label");
					        optional_string(condition, "description");
					        edit_registered(condition, "gameplay-editors.equipment.colour");
					        edit_registered(condition, "gameplay-editors.equipment.glow");
					        optional_string(condition, "siren");
					        optional_string(condition, "music");
					        edit_shared_field(label("music_gain").c_str(), condition,
					                          "gameplay-editors.equipment.music_gain");
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
	tool_prepare_action("Add System");
	ImGui::InputText(label("New System identity").c_str(), &new_system);
	tool_action_line("Add System");
	if (button("Add System") && !new_system.empty())
	{
		(void)workshop_custom_action(ship, "system.add", {{"name", new_system}});
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
			edit_registered(spec, "gameplay-editors.equipment.kind.2");
			auto kind = equipment_kind(document, spec);
			if (kind.empty())
				kind = selected_system;
			system_details(document, spec, kind);
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
	records(ship, "stations", workshop_record("gameplay-editors.equipment.stations"),
	        [&](Json &station)
	        {
		        edit_registered(station, "gameplay-editors.equipment.id");
		        records(station, "consoles", workshop_record("gameplay-editors.equipment.consoles"),
		                [&](Json &console)
		                {
			                edit_registered(console, "gameplay-editors.equipment.id");
			                choice(console, "definition", document.definition_choices(WorkshopDefinitionKind::Console));
			                names(console, "systems", systems);
			                names(console, "feeds", systems);
			                edit_registered(console, "gameplay-editors.equipment.shield_feed");
			                const auto stations = ids(ship.at("stations"));
			                std::vector<std::string> defaults{station.at("id").get<std::string>()};
			                for (const auto &id : stations)
				                if (id != defaults.front())
					                defaults.push_back(id);
			                ranked_names(console, "human_preferences", stations, defaults);
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
		records(*ship, "assemblies", workshop_record("gameplay-editors.equipment.assemblies"),
		        [&](Json &placement)
		        {
			        edit_registered(placement, "gameplay-editors.equipment.id");
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
	tool_prepare_action("Add endpoint");
	ImGui::InputText(label("Service name").c_str(), &names[identity]);
	tool_action_line("Add endpoint");
	if (button("Add endpoint") && !names[identity].empty())
	{
		(void)workshop_custom_action(parent, "service.add", {{"name", names[identity]}, {"direction", "output"}});
	}
	if (parent.contains("services"))
		object_rows(parent["services"],
		            [&](const std::string &name, Json &row)
		            {
			            ImGui::TextUnformatted(name.c_str());
			            edit_registered(row, "gameplay-editors.equipment.kind.3");
			            edit_registered(row, "gameplay-editors.equipment.direction");
			            edit_registered(row, "gameplay-editors.equipment.target.2");
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
