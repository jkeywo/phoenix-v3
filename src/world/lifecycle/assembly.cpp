module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.world.assembly;
import phoenix.content.assets;
import phoenix.content.combat;
import phoenix.content.controllers;
import phoenix.content.effects;
import phoenix.content.equipment;
import phoenix.content.logistics;
import phoenix.content.operations;
import phoenix.content.resources;
import phoenix.content.sensing;
import phoenix.control.templates;
import phoenix.flight;
import phoenix.resources;

namespace phoenix
{
EntityAssembly compile_world_entity(const Catalog &source, const Catalog &content, const std::string &name,
                                    const DirectedCatalog &directed_catalog, const RulesCatalog &rule_templates,
                                    SharedPropertyCatalog property_catalogue,
                                    const SharedAbilityCatalog &ability_catalog,
                                    const SharedProcessCatalog &process_catalog,
                                    SharedResourceCatalogue resource_catalogue)
{
	const auto &definition = content.entities.at(name);
	EntityAssembly assembly;
	auto &result = assembly.definition;
	result.audio = compile_ship_audio(content, name);
	result.properties.bases = compile_property_bindings(*property_catalogue, definition, "entities." + name);
	for (const auto &[key, fit] : definition.at("systems").items())
		result.equipment_properties[key].bases =
		    compile_property_bindings(*property_catalogue, fit, "entities." + name + ".systems." + key);
	if (definition.contains("personnel") && definition.at("personnel").is_object())
		for (const auto &officer : definition.at("personnel").value("officers", Json::array()))
			result.officer_properties[officer.at("id").get<std::string>()].bases =
			    compile_property_bindings(*property_catalogue, officer, "entities." + name + ".officers");
	if (definition.contains("combat"))
		for (const auto &deck : definition.at("combat").value("decks", Json::array()))
			for (const auto &compartment : deck.at("compartments"))
				result.compartment_properties[compartment.at("id").get<std::string>()].bases =
				    compile_property_bindings(*property_catalogue, compartment, "entities." + name + ".compartments");
	result.combat = compile_combat(content, name, directed_catalog, rule_templates);
	if (result.combat)
		result.destruction = result.combat->destruction;
	if (definition.contains("destruction"))
		result.destruction =
		    compile_destruction(definition.at("destruction"), content, "entities." + name + ".destruction");
	result.durability = definition.value("durability", std::int64_t{0});
	result.half_height =
	    definition.value("half_height_mm", result.combat ? result.combat->half_height : std::int64_t{5000});
	if (result.durability < 0 || result.durability > 1000000 || result.half_height < 1 || result.half_height > 1000000)
		throw std::runtime_error("Invalid entity durability/vertical extent");
	assembly.visual_scale = definition.at("render").at("scale");
	result.bridge = compile_bridge(content, name);
	result.personnel = compile_personnel(content, name, directed_catalog, rule_templates, property_catalogue);
	result.rules = compile_source_rules(content, name, rule_templates);
	const auto ability_layout = compile_controllable_layout(content, name, property_catalogue);
	result.psychic = compile_psychic(content, name, ability_catalog, result.personnel, ability_layout);
	result.abilities =
	    compile_abilities(content, name, ability_catalog, result.personnel, ability_layout, result.psychic);
	result.processes = compile_process_bindings(content, name, process_catalog, ability_layout, property_catalogue);
	result.boarding = compile_boarding(content, name, directed_catalog, property_catalogue);
	for (const auto &row : definition.value("boarding_missions", Json::array()))
		result.boarding_docking_ports[row.at("id").get<std::string>()] =
		    row.value("docking_ports", std::vector<std::string>{});
	result.transport = compile_transport(content, name, directed_catalog, property_catalogue);
	result.logistics = compile_logistics(content, name, directed_catalog, property_catalogue);
	result.processors = compile_processors(content, name, directed_catalog, property_catalogue);
	result.docking = compile_docking(content, name, directed_catalog, property_catalogue);
	result.craft = compile_craft(content, name);
	result.craft_bays = compile_craft_bays(content, name, directed_catalog, property_catalogue);
	result.ftl = compile_ftl(content, name);
	for (const auto &[key, s] : definition.at("systems").items())
		if (s.value("kind", key) == "displacement" || s.value("kind", key) == "transport")
		{
			DisplacementDefinition t{s.at("compartment"), s.at("range_mm"), s.at("capacity"), s.at("transfer_ticks")};
			result.displacements[key] = t;
		}
	for (const auto &console : resolve_consoles(content, name))
		if (!console.definition.empty())
			result.console_bridge.emplace(
			    console.id, compile_console_bridge(content.consoles.at(console.definition), console.definition));
	if (definition.contains("control_layout"))
	{
		result.layout = ability_layout;
		result.layout->converted = !source.entities.at(name).contains("control_layout");
		result.controller_templates = compile_controller_templates(content, *result.layout);
	}
	for (const auto &publication : definition.value("publications", Json::array()))
		result.publications.push_back(publication.at("id"));
	for (const auto &message : definition.value("message_templates", Json::array()))
		result.message_templates.push_back(message);
	result.sensors = compile_sensors(content, name, directed_catalog);
	result.directed = directed_catalog.at(name);
	result.sensor_class = definition.value("class_key", definition.value("name", std::string{"Unknown class"}));
	result.tags = definition.value("tags", std::vector<std::string>{"contact"});
	result.faction = definition.value("faction", std::string{});
	for (const auto &[sensor_id, sensor] : result.sensors)
		result.radar_shows[sensor_id] =
		    definition.at("systems").at(sensor_id).value("shows", std::vector<std::string>{"contact"});
	result.radar_icon = render_radar_icon(definition.at("render"));
	result.radar_sprite = render_radar_sprite(definition.at("render"), content);
	if (definition.at("render").contains("sprite"))
	{
		const auto sprite = compile_sprite(definition.at("render").at("sprite"), content, name);
		result.sprite_entry = sprite_entry_index(sprite, definition.at("render").value("sprite_entry", std::string{}));
		for (const auto &entry : sprite.entries)
			result.sprite_entries.push_back(entry.name);
		if (result.sprite_entries.empty())
			result.sprite_entries.push_back("default");
	}
	result.consoles = resolve_consoles(content, name);
	if (result.layout)
		for (const auto &fit : resolve_systems(content, name))
			result.observation_owners[fit.id] =
			    definition.at("systems").at(fit.id).value("observation_console", result.consoles.front().id);

	result.explicit_stations = definition.contains("stations");
	const auto &collision = definition.at("collision");
	result.collider.mass = collision.at("mass_grams");
	result.collider.half_height = result.half_height;
	result.collider.spatial = result.craft.has_value();
	result.collider.box = collision.at("shape") == "box";
	if (result.collider.box)
	{
		result.collider.half_width = collision.at("half_width_mm");
		result.collider.half_length = collision.at("half_length_mm");
	}
	else
		result.collider.radius = collision.at("radius_mm");
	const auto &systems = definition.at("systems");
	if (result.layout)
		result.fitted.emplace();
	for (const auto &spec : resolve_systems(content, name))
		if (spec.id != operating_system_name(spec.kind) || systems.at(spec.id).contains("kind"))
			result.fitted.emplace();
	const bool advanced = uses_advanced_machinery(content, name);
	if (advanced)
	{
		result.fitted.emplace();
		result.gameplay.emplace();
		if (auto power = compile_ship_resources(content, name))
		{
			power->network.resources = resource_catalogue;
			result.gameplay->power = std::move(*power);
		}
		for (const auto &spec : resolve_systems(content, name))
		{
			CompiledGameplaySystem runtime;
			if (spec.kind == OperatingSystem::Engine || spec.kind == OperatingSystem::Steering ||
			    spec.kind == OperatingSystem::Lateral || spec.kind == OperatingSystem::Boost ||
			    spec.kind == OperatingSystem::Impulse)
			{
				auto data = systems.at(spec.id);
				data["kind"] = std::string(operating_system_name(spec.kind));
				runtime.flight = compile_flight(data, "entities." + name + ".systems." + spec.id);
			}
			result.gameplay->values.push_back(std::move(runtime));
		}
		std::vector<FlightDefinition> flights;
		std::vector<std::string> names;
		const auto fits = resolve_systems(content, name);
		for (std::size_t i = 0; i < fits.size(); ++i)
		{
			FlightDefinition absent;
			absent.kind = FlightKind::Lateral;
			flights.push_back(result.gameplay->values[i].flight.value_or(absent));
			names.push_back(fits[i].id);
		}
		resolve_flight_modifiers(flights, names);
		for (std::size_t i = 0; i < fits.size(); ++i)
			if (result.gameplay->values[i].flight)
				result.gameplay->values[i].flight = std::move(flights[i]);
	}
	if (result.fitted)
	{
		for (const auto &spec : resolve_systems(content, name))
		{
			CompiledEquipment runtime{spec.id, spec.kind};
			if (spec.kind == OperatingSystem::Engine)
			{
				runtime.engine = Engine{spec.maximum, spec.response};
				runtime.engine->deceleration = systems.at(spec.id).value("deceleration_mm_s2", spec.response);
			}
			else if (spec.kind == OperatingSystem::Steering)
				runtime.steering = Steering{spec.response};
			else if (spec.kind == OperatingSystem::Radar)
				runtime.radar = Radar{spec.maximum};
			runtime.advanced = result.gameplay.has_value();
			runtime.maximum = spec.maximum;
			runtime.enabled = systems.at(spec.id).value("enabled", true);
			if (runtime.radar)
				runtime.radar->enabled = runtime.enabled;
			result.fitted->values.push_back(std::move(runtime));
		}
	}
	if (std::any_of(result.fitted->values.begin(), result.fitted->values.end(),
	                [](const auto &s)
	                {
		                return s.kind == OperatingSystem::Comms;
	                }))
	{
		const auto &spec = content.entities.at(name);
		const auto comms = spec.value("comms", Json::object());
		const auto tree = comms.value("default_conversation", std::string{});
		auto &roster = assembly.participants;
		roster.push_back({"", spec.value("name", name), "", tree, 0, false, true});
		for (const auto &p : comms.value("characters", Json::array()))
			roster.push_back(
			    {p.at("id"), p.at("name_key"),
			     p.value("portrait", std::string{}).empty()
			         ? std::string{}
			         : "console-assets:assets/comms/" + sha256(content.assets.at(p.at("portrait"))) + ".png",
			     p.value("default_conversation", tree), 0, true, p.value("public", true)});
	}
	return assembly;
}
} // namespace phoenix
