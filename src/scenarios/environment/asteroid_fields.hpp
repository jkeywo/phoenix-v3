// Included inside World::Impl. Authoritative generation uses integer arithmetic.
static constexpr EntityId field_identity_base = EntityId{1} << 52;
std::map<EntityId, FieldRock> field_rocks;
AsteroidWindowCache field_window;
std::uint64_t field_seed = 1;
void load_rock(FieldRock &rock)
{
	if (rock.loaded)
		return;
	const auto &s = rock.scene;
	if (!rock.decorative)
	{
		const auto &d = definitions.at(s.definition);
		auto entity = ecs.entity()
		                  .set<StableId>({s.id})
		                  .set<Definition>({s.definition, d.radar_icon, d.radar_sprite})
		                  .set<Pose>({{s.x, s.y}, s.heading})
		                  .set<Motion>({rock.velocity, rock.remainder})
		                  .set<Collider>(d.collider);
		if (d.collider.mass)
			entity.add<Movable>();
		entities.emplace(s.id, entity);
		sprite_choices[s.id] = s.sprite_entry;
	}
	rock.loaded = true;
}
void unload_rock(FieldRock &rock)
{
	if (!rock.loaded)
		return;
	if (!rock.decorative)
	{
		auto entity = entities.at(rock.scene.id);
		const auto &p = entity.get<Pose>();
		const auto &m = entity.get<Motion>();
		rock.scene.x = p.position.x;
		rock.scene.y = p.position.y;
		rock.scene.heading = p.heading;
		rock.velocity = m.velocity;
		rock.remainder = m.remainder;
		entity.destruct();
		entities.erase(rock.scene.id);
		sprite_choices.erase(rock.scene.id);
	}
	rock.loaded = false;
}
void stream_fields()
{
	std::int64_t spacing = 0, visual = 0, margin = 0, rock_extent = 0, rock_travel = 0;
	for (const auto &r : regions)
		if (r.asteroids)
		{
			spacing = r.asteroids->spacing;
			visual = std::max(visual, r.asteroids->visual_distance);
			margin = std::max(margin, r.asteroids->unload_margin);
			for (const auto &type : r.asteroids->types)
			{
				const auto &shape = definitions.at(type.definition).collider;
				rock_extent = std::max(rock_extent, shape.box ? shape.half_width + shape.half_length : shape.radius);
			}
		}
	if (!spacing)
		return;
	for (const auto &[id, rock] : field_rocks)
		if (!rock.decorative)
		{
			const auto velocity = rock.loaded ? entities.at(id).get<Motion>().velocity : rock.velocity;
			rock_travel = std::max(rock_travel, (fixed::length(velocity) + rate - 1) / rate);
		}
	std::vector<AsteroidFocus> foci;
	for (const auto &[id, entity] : entities)
		if (id < field_identity_base && entity.has<EntityControls>())
		{
			auto radius = visual;
			if (definition(id).fitted)
				for (const auto &fit : definition(id).fitted->values)
					if (fit.kind == OperatingSystem::Radar)
						radius = std::max(radius, fit.radar ? fit.radar->range : fit.maximum);
			const auto &d = definition(id).collider;
			radius += fixed::length(entity.get<Motion>().velocity) / rate +
			          (d.box ? d.half_width + d.half_length : d.radius) + rock_extent + rock_travel + spacing * 2;
			foci.push_back({entity.get<Pose>().position, radius});
		}
	// Shots remain physical beyond their source ship's sensor/visual window.
	// Include the complete next-tick sweep before integrating either body.
	for (auto particle : projectile_rows())
	{
		const auto &p = particle.get<Projectile>();
		const fixed::Vec travel{(p.remainder.x + p.velocity.x) / rate, (p.remainder.y + p.velocity.y) / rate};
		foci.push_back({p.position, fixed::length(travel) + rock_extent + rock_travel + spacing * 2 + 1});
	}
	auto window = field_window.prepare(foci, spacing);
	for (auto &[id, rock] : field_rocks)
	{
		const auto p = rock.decorative || !rock.loaded ? fixed::Vec{rock.scene.x, rock.scene.y}
		                                               : entities.at(id).get<Pose>().position;
		if (rock.loaded && std::none_of(foci.begin(), foci.end(),
		                                [&](const auto &f)
		                                {
			                                return fixed::length(p - f.position) <= f.radius + margin;
		                                }))
			unload_rock(rock);
		else if (!rock.loaded && std::any_of(foci.begin(), foci.end(),
		                                     [&](const auto &f)
		                                     {
			                                     return fixed::length(p - f.position) <= f.radius;
		                                     }))
			load_rock(rock);
	}
	if (!window.changed)
		return;
	for (const auto &[x, y] : window.cells)
	{
		if (field_window.contains(x, y))
			continue;
		for (auto generated : generate_asteroid_cell(regions, field_seed, x, y))
		{
			const auto id = generated.scene.id;
			if (auto found = field_rocks.find(id); found != field_rocks.end())
			{
				const auto &r = found->second;
				const fixed::Vec p{r.scene.x, r.scene.y};
				if (r.loaded || std::any_of(foci.begin(), foci.end(),
				                            [&](const auto &f)
				                            {
					                            return fixed::length(p - f.position) <= f.radius;
				                            }))
					load_rock(found->second);
				continue;
			}
			if (field_rocks.size() >= 200000)
				throw std::runtime_error("Asteroid persistence exceeds 200000 rocks");
			FieldRock rock;
			rock.scene = generated.scene;
			rock.decorative = generated.decorative;
			const auto &d = definitions.at(rock.scene.definition);
			rock.scene.sprite_entry = d.sprite_entry;
			if (!generated.entry.empty())
				rock.scene.sprite_entry = static_cast<std::uint32_t>(
				    std::find(d.sprite_entries.begin(), d.sprite_entries.end(), generated.entry) -
				    d.sprite_entries.begin());
			load_rock(field_rocks.emplace(id, std::move(rock)).first->second);
		}
	}
	field_window.commit(std::move(window));
}
Json save_fields() const
{
	Json out = Json::array();
	for (const auto &[id, source] : field_rocks)
	{
		auto rock = source;
		if (rock.loaded && !rock.decorative)
		{
			const auto entity = entities.at(id);
			const auto &pose = entity.get<Pose>();
			const auto &motion = entity.get<Motion>();
			rock.scene.x = pose.position.x;
			rock.scene.y = pose.position.y;
			rock.scene.heading = pose.heading;
			rock.velocity = motion.velocity;
			rock.remainder = motion.remainder;
		}
		out.push_back(encode_asteroid_field(rock));
	}
	return out;
}
void restore_fields(const Json &saved)
{
	if (!saved.is_array() || saved.size() > 200000)
		throw std::runtime_error("Invalid saved asteroid field population");
	for (auto &[id, r] : field_rocks)
		unload_rock(r);
	field_rocks.clear();
	field_window.clear();
	for (const auto &v : saved)
	{
		auto r = decode_asteroid_field(v);
		const auto &s = r.scene;
		const bool known = definitions.contains(s.definition);
		validate_asteroid_field(r, regions, known, known ? definitions.at(s.definition).sprite_entries.size() : 0,
		                        field_rocks.contains(s.id));
		auto &inserted = field_rocks.emplace(s.id, std::move(r)).first->second;
		if (v.at("loaded").get<bool>())
			load_rock(inserted);
	}
}
