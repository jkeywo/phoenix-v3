#pragma once

// Private codec for the browser executable and its ordinary WASM host.
namespace phoenix
{
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LobbySlot, id, ship, host, mandatory, allowed_ships, briefing)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LobbyMember, id, slot, console, role, approved, connected, ready, verified, name,
                                   avatar, local_screen)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ConsoleIntroduction, responsibilities, automation, systems)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LobbyConsole, slot, id, name, member, local_screen, ready, profile, profiles,
                                   description, profile_stars, introductions)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LobbyProjection, active, coordinator, launched, self, scenario, code, status,
                                   scenario_name, countdown_ms, slots, members, consoles)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(LobbyCommand, action, member, slot, ship, console, screen, profile)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ConsoleSpec, id, station, definition, controls, feeds, controlled_systems,
                                   feed_systems, human_seeking, human_preferences, shield_feed, docking_feed)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ConsoleSeat, address, spec, available)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(BaseConfiguration, id, hash, render_mode, interface_style)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(BodyView, id, definition, x, y, vx, vy, heading, radar_icon, radar_sprite, faction,
                                   faction_label, disposition, altitude, vz, radius)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SceneAttachment, id, definition, marker, heading, scale, flip_y)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SceneMuzzle, weapon, turret, marker, x, y)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(AudioClip, asset, gain)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SceneAudio, kind, source, clip, event, tick, peak_gain, decay, damage, threshold,
                                   full_damage, own_only, retention_ticks)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SceneShieldImpact, event, tick, time, absorbed, direction, path_point, has_path)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(SceneBody, id, definition, x, y, vx, vy, heading, radar_icon, radar_sprite, faction,
                                   faction_label, disposition, altitude, vz, radius, shield_impacts, shield_time,
                                   shield_radius, audio_alive, audio_only, audio, forward_thrust, sprite_entry,
                                   visual_height_mm, rotation_mdeg, spin_mdeg_s, field_instance, attachments, muzzles,
                                   attack_source, attack_weapon, muzzle_blend, muzzle_tail_blend, visual_scale, alpha,
                                   sound, effect_tick, visual_vz, line_end_x, line_end_y, line_end_z, line_style,
                                   velocity_rotation, smoke_trail)

inline void to_json(Json &j, const WidgetReadoutBinding &value)
{
	j = {{"source", value.source},        {"system_field", value.system_field}, {"role", value.role},
	     {"field", value.field},          {"display_path", value.display_path}, {"pointer", value.pointer.to_string()},
	     {"rows", value.rows.to_string()}};
	if (value.telemetry)
		j["telemetry"] = *value.telemetry;
}
inline void from_json(const Json &j, WidgetReadoutBinding &value)
{
	j.at("source").get_to(value.source);
	j.at("system_field").get_to(value.system_field);
	value.role = j.at("role");
	value.field = j.at("field");
	value.display_path = j.at("display_path");
	value.pointer = Json::json_pointer(j.at("pointer").get<std::string>());
	value.rows = Json::json_pointer(j.at("rows").get<std::string>());
	if (j.contains("telemetry"))
		value.telemetry = j.at("telemetry").get<TelemetryField>();
}

// Only the data required by the page's scene and Console presentation. World
// definitions and verified archives stay owned by the worker.
inline Json host_render_catalog(const Catalog &catalog)
{
	Json entities = Json::object(), maps = Json::object();
	for (const auto &[id, entity] : catalog.entities)
	{
		auto &row = entities[id];
		for (const char *key :
		     {"id", "name", "render", "control_layout", "stations", "console", "systems", "assemblies"})
			if (entity.contains(key))
				row[key] = entity.at(key);
	}
	for (const auto &[id, map] : catalog.maps)
	{
		auto &row = maps[id];
		for (const char *key : {"id", "name", "presentation", "lighting", "ship_slots", "loading_background"})
			if (map.contains(key))
				row[key] = map.at(key);
	}
	return {{"entities", std::move(entities)},
	        {"maps", std::move(maps)},
	        {"consoles", catalog.consoles},
	        {"layouts", catalog.layouts},
	        {"ui_groups", catalog.ui_groups},
	        {"themes", catalog.themes},
	        {"control_widgets", catalog.control_widgets},
	        {"widget_readouts", catalog.widget_readouts},
	        {"widget_data", catalog.widget_data},
	        {"widget_instances", catalog.widget_instances},
	        {"equipment", catalog.equipment},
	        {"assemblies", catalog.assemblies},
	        {"base", catalog.base},
	        {"ordered_packs", catalog.ordered_packs},
	        {"loading_background", catalog.loading_background},
	        {"ui_theme", catalog.ui_theme}};
}

inline Catalog read_host_render_catalog(const Json &data)
{
	Catalog catalog;
#define PHOENIX_READ_FIELD(field) data.at(#field).get_to(catalog.field)
	PHOENIX_READ_FIELD(entities);
	PHOENIX_READ_FIELD(maps);
	PHOENIX_READ_FIELD(consoles);
	PHOENIX_READ_FIELD(layouts);
	PHOENIX_READ_FIELD(ui_groups);
	PHOENIX_READ_FIELD(themes);
	PHOENIX_READ_FIELD(control_widgets);
	PHOENIX_READ_FIELD(widget_readouts);
	PHOENIX_READ_FIELD(widget_data);
	PHOENIX_READ_FIELD(widget_instances);
	PHOENIX_READ_FIELD(equipment);
	PHOENIX_READ_FIELD(assemblies);
	PHOENIX_READ_FIELD(base);
	PHOENIX_READ_FIELD(ordered_packs);
	PHOENIX_READ_FIELD(loading_background);
	PHOENIX_READ_FIELD(ui_theme);
#undef PHOENIX_READ_FIELD
	return catalog;
}
} // namespace phoenix
