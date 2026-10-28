module;
#include "native_headers.hpp"

module phoenix.simulation;
import :bindings;

namespace phoenix
{
using namespace world_detail;
std::vector<SceneBody> World::scene() const
{
	std::vector<SceneBody> result;
	for (const auto &body : inspect())
	{
		SceneBody item;
		static_cast<BodyView &>(item) = body;
		item.sprite_entry = impl_->sprite_choices.at(body.id);
		item.visual_height_mm = impl_->altitude(body.id);
		if (const auto *v = impl_->entities.at(body.id).try_get<VerticalMotion>())
			item.visual_vz = v->velocity;
		if (auto found = impl_->field_rocks.find(body.id); found != impl_->field_rocks.end())
		{
			const auto &s = found->second.scene;
			item.field_instance = true;
			item.rotation_mdeg = s.rotation_mdeg;
			item.spin_mdeg_s = s.spin_mdeg_s;
		}
		if (auto systems = fittings(impl_->entities.at(body.id)))
			for (const auto &system : systems->values)
				if (system.engine && system.engine->enabled)
					item.forward_thrust = std::max(item.forward_thrust, system.engine->forward_output);
		if (auto systems = gameplay_systems(impl_->entities.at(body.id)))
			for (const auto &system : systems->values)
				if (system.flight)
					item.forward_thrust =
					    std::max(item.forward_thrust, std::max(0, system.flight->state.actual_longitudinal));
		const auto entity = impl_->entities.at(body.id);
		item.audio_alive = !impl_->combat_dead(entity);
		if (item.audio_alive)
		{
			if (auto at = impl_->audio_events.find(body.id); at != impl_->audio_events.end())
				item.audio = at->second;
			if (const auto *shields = entity.try_get<ShieldComponent>())
				for (const auto &c : shields->contributions)
				{
					const auto &d = c.definition.shield;
					if (!d.audio.asset.empty() && impl_->shield_available(entity, c) && c.state.warmup &&
					    c.state.strength && c.protection > 0)
					{
						SceneAudio cue{SceneAudioKind::Shield, "shield", d.audio};
						cue.peak_gain = d.audio_peak_gain;
						cue.decay = d.audio_decay;
						cue.threshold = d.audio_threshold;
						cue.full_damage = d.audio_full_damage;
						item.audio.push_back(std::move(cue));
					}
				}
		}
		if (impl_->combat_dead(entity) && !entity.has<CombatStructure>())
			continue;
		if (const auto *structure = entity.try_get<CombatStructure>())
		{
			if (impl_->combat_dead(entity))
			{
				const auto &death = entity.get<Destructible>();
				if (static_cast<std::int64_t>(ticks()) - death.destroyed_tick >= death.definition.growth_ticks)
					continue;
				item.forward_thrust = 0;
			}
			for (auto child : entity.get<EquipmentIndex>().values)
				if (const auto *m = child.try_get<CombatMechanism>();
				    m && m->definition.kind == OperatingSystem::Turret)
				{
					const auto &d = m->definition;
					item.attachments.push_back(
					    {d.id, d.get<TurretDefinition>().visual, d.get<TurretDefinition>().marker,
					     combat_angle(body.heading + m->get<TurretState>().heading),
					     d.get<TurretDefinition>().scale_permille / 1000.f, d.get<TurretDefinition>().flip_y});
				}
		}
		if (entity.has<CombatStructure>())
			for (auto child : entity.get<EquipmentIndex>().values)
				if (const auto *m = child.try_get<CombatMechanism>();
				    m && m->definition.kind == OperatingSystem::Weapon)
				{
					const auto &w = m->definition.get<WeaponDefinition>();
					const auto [origin, heading] = impl_->weapon_mount(entity, *m);
					if (heading >= 0 && !w.muzzle_marker.empty())
						item.muzzles.push_back({m->definition.id, w.turret, w.muzzle_marker, origin.x, origin.y});
				}
		result.push_back(std::move(item));
	}
	const auto source_count = result.size();
	for (const auto particle : impl_->projectile_rows())
	{
		const auto &p = particle.get<Projectile>();
		SceneBody shot;
		shot.id = 7000000000000000ULL + p.id;
		shot.definition = p.definition.visual;
		shot.x = p.position.x;
		shot.y = p.position.y;
		shot.vx = p.velocity.x;
		shot.vy = p.velocity.y;
		shot.heading = p.heading;
		shot.attack_source = p.source;
		shot.attack_weapon = p.weapon;
		shot.muzzle_blend = std::max(0.f, 1.f - p.age / 4.f);
		shot.muzzle_tail_blend = std::max(0.f, 1.f - (p.age - 1) / 4.f);
		shot.visual_height_mm = p.altitude;
		shot.visual_vz = p.vz;
		shot.velocity_rotation = p.definition.durability > 0;
		shot.smoke_trail = !p.definition.trail.empty();
		if (p.definition.tracer)
		{
			shot.line_style = 2;
			shot.line_end_x = p.position.x - p.velocity.x / rate;
			shot.line_end_y = p.position.y - p.velocity.y / rate;
			shot.line_end_z = p.altitude - p.vz / rate;
		}
		result.push_back(shot);
	}
	for (const auto &[id, ship] : impl_->combat_rows())
		for (auto child : ship.get<EquipmentIndex>().values)
			if (const auto *m = child.try_get<CombatMechanism>();
			    m && m->definition.kind == OperatingSystem::Weapon &&
			    m->definition.get<WeaponDefinition>().family == "beam" && m->get<WeaponState>().beam_tick >= 0 &&
			    m->get<WeaponState>().beam_tick + 1 == static_cast<std::int64_t>(ticks()))
			{
				const auto &clip = impl_->directed_mode(id, m->definition.id)->firing_audio;
				const auto end = result.begin() + source_count;
				const auto source = std::lower_bound(result.begin(), end, id,
				                                     [](const auto &body, EntityId value)
				                                     {
					                                     return body.id < value;
				                                     });
				if (!clip.asset.empty() && source != end && source->id == id && source->audio_alive)
					source->audio.push_back({SceneAudioKind::Loop, "weapon:" + m->definition.id, clip});
				const auto [origin, heading] = impl_->weapon_mount(ship, *m);
				SceneBody beam;
				beam.id = 6000000000000000ULL + id * 256 + child.get<Equipment>().ordinal;
				beam.definition = ship.get<Definition>().name;
				beam.x = origin.x;
				beam.y = origin.y;
				beam.line_end_x = m->get<WeaponState>().beam_end.x;
				beam.line_end_y = m->get<WeaponState>().beam_end.y;
				beam.line_style = 1;
				beam.attack_source = id;
				beam.attack_weapon = m->definition.id;
				beam.muzzle_blend = 1;
				result.push_back(beam);
			}
	for (const auto &v : impl_->blast_visuals)
	{
		const auto age = static_cast<std::int64_t>(ticks()) - v.tick;
		const auto &d = v.definition;
		SceneBody visual;
		visual.id = 8500000000000000ULL + v.id;
		visual.definition = d.visual;
		visual.x = v.position.x;
		visual.y = v.position.y;
		visual.visual_height_mm = v.altitude;
		visual.visual_scale = static_cast<float>(d.size) / 1000.f /
		                      std::max(.001f, d.visual.empty() ? 1.f : impl_->visual_scales.at(d.visual)) *
		                      std::min(1.f, std::max(.01f, static_cast<float>(age) / d.growth_ticks));
		visual.alpha =
		    age < d.growth_ticks ? 1.f : std::max(0.f, 1.f - static_cast<float>(age - d.growth_ticks) / d.fade_ticks);
		visual.sound = d.sound;
		visual.effect_tick = v.tick;
		if (v.kind == 1)
		{
			visual.line_style = 3;
			visual.visual_scale = d.size / 1000.f;
			visual.heading = v.bearing;
			visual.rotation_mdeg = v.arc;
		}
		result.push_back(visual);
	}
	for (const auto &[id, rock] : impl_->field_rocks)
		if (rock.loaded && rock.decorative)
			result.push_back(rock.scene);
	return result;
}

Json World::scene_environment() const
{
	auto environment = region_environment(impl_->regions, impl_->environment_tick / static_cast<double>(rate));
	environment["simulation_step"] = 1.0 / rate;
	return environment;
}

Json World::public_board(EntityId entity) const
{
	auto board = impl_->knowledge.public_board(entity);
	board["alert"] = impl_->alert_board(entity);
	board["navigation"] = impl_->navigation_board(entity);
	board["objective_board"] = impl_->objectives_board(entity);

	if (impl_->combat_duel && impl_->duel_ended())
	{
		int alive = 0;
		for (const auto &[id, ship] : impl_->combat_rows())
			if (!impl_->combat_dead(ship))
				++alive;
		board["combat_result"] = alive == 0                                       ? "Draw"
		                         : impl_->combat_dead(impl_->entities.at(entity)) ? "Defeat"
		                                                                          : "Victory";
	}

	board["sidebar"] = Json::array();
	const auto &bridge_data = impl_->definition(entity);
	for (const auto &seat : bridge_data.consoles)
		if (bridge_data.console_bridge.contains(seat.id) && bridge_data.console_bridge.at(seat.id).viewscreen)
			board["sidebar"].push_back({{"id", seat.id},
			                            {"label", impl_->console_layouts.at(seat.definition).value("name", seat.id)},
			                            {"default", bridge_data.bridge.sidebar_default == seat.id},
			                            {"visible", impl_->bridge_visible({entity, seat.id})},
			                            {"available", can_claim(UINT64_MAX, entity, seat.id, true)}});
	const auto found = impl_->displays.find(entity);
	if (found == impl_->displays.end())
		return board;
	const auto &publication = found->second;
	if (!impl_->ownership.claims().contains(publication.claimant) ||
	    (impl_->ownership.claims().at(publication.claimant) != publication.address &&
	     impl_->console_operator(publication.address) != publication.claimant))
		return board;
	const auto widget = impl_->display_widget(publication.address, publication.widget);
	if (!widget)
		return board;
	const auto view = impl_->capture_console_observation(publication.address);
	const bool radar = widget->at("kind") == "radar";
	Json value{{"revision", publication.revision}, {"console", publication.address.console},
	           {"widget", publication.widget},     {"kind", widget->at("kind")},
	           {"camera", publication.camera},     {"own", display::body_json(view.own)},
	           {"contacts", Json::array()},        {"regions", Json::array()},
	           {"waypoint", Json::object()},       {"objectives", Json::array()}};
	if (widget->at("kind") == "conversations")
	{
		auto projection = impl_->conversations.project(entity);
		value["conversation"] = Json::object();
		for (const auto &thread : projection.at("threads"))
			if (thread.at("id") == projection.at("selected"))
				value["conversation"] = thread;
		board["display"] = display::sanitise(value);
		return board;
	}
	const SystemView *sensor = nullptr;
	if (radar)
	{
		const auto selected =
		    select_system(view.systems, OperatingSystem::Radar, widget->value("system", std::string{}), false);
		for (const auto &system : view.systems)
			if (selected && system.id == *selected)
				sensor = &system;
		if (!sensor)
			return board;
		value["radar"] = display::radar_json(*sensor);
		value["sweep"] = widget->value("sweep", Json::object());
		value["bearing_contacts"] = Json::array();
		if (sensor->enabled)
			for (const auto &b : view.bearing_contacts)
				if (std::find(sensor->contact_ids.begin(), sensor->contact_ids.end(), b.id) !=
				    sensor->contact_ids.end())
					value["bearing_contacts"].push_back(projection::bearing_json(b));
		if (sensor->enabled)
			for (const auto &contact : view.contacts)
				if (std::find(sensor->contact_ids.begin(), sensor->contact_ids.end(), contact.id) !=
				    sensor->contact_ids.end())
					value["contacts"].push_back(display::body_json(contact));
	}
	for (const auto &region : view.regions)
		if (region.value(radar ? "radar" : "navigation", false) &&
		    (!radar || std::any_of(region.at("tags").begin(), region.at("tags").end(),
		                           [&](const auto &tag)
		                           {
			                           return std::find(sensor->shows.begin(), sensor->shows.end(),
			                                            tag.template get<std::string>()) != sensor->shows.end();
		                           })))
		{
			Json marker = region;
			marker["tags"] = Json::array();
			if (radar)
				for (const auto &tag : region.at("tags"))
					if (std::find(sensor->shows.begin(), sensor->shows.end(), tag.get<std::string>()) !=
					    sensor->shows.end())
						marker["tags"].push_back(tag);
			value["regions"].push_back(std::move(marker));
		}
	std::map<std::uint64_t, Json> objectives;
	for (const auto &record : board.value("shared", Json::array()))
		if (record.value("schema", "") == "phoenix.waypoint")
			value["waypoint"] = record.at("fields");
		else if (!radar && record.value("schema", "") == "phoenix.objective" &&
		         record.at("fields").contains("destination_x"))
			objectives[record.at("fields").at("id").get<std::uint64_t>()] = record.at("fields");
	for (const auto &[id, fields] : objectives)
		value["objectives"].push_back(fields);
	board["display"] = display::sanitise(value);
	return board;
}
} // namespace phoenix
