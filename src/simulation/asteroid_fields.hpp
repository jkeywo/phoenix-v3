// Included inside World::Impl. Authoritative generation uses integer arithmetic.
static constexpr EntityId field_identity_base = EntityId{1} << 52;
struct FieldRock
{
	SceneBody scene{};
	fixed::Vec velocity{}, remainder{};
	bool decorative = false, loaded = false;
};
std::map<EntityId, FieldRock> field_rocks;
std::uint64_t field_seed = 1;
static std::int64_t floor_cell(std::int64_t n, std::int64_t spacing)
{
	return n >= 0 ? n / spacing : -((-n + spacing - 1) / spacing);
}
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
	std::int64_t spacing = 0, visual = 0, margin = 0;
	for (const auto &r : regions)
		if (r.asteroids)
		{
			spacing = r.asteroids->spacing;
			visual = std::max(visual, r.asteroids->visual_distance);
			margin = std::max(margin, r.asteroids->unload_margin);
		}
	if (!spacing)
		return;
	struct Focus
	{
		fixed::Vec position;
		std::int64_t radius;
	};
	std::vector<Focus> foci;
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
			          (d.box ? d.half_width + d.half_length : d.radius) + spacing * 2;
			foci.push_back({entity.get<Pose>().position, radius});
		}
	std::set<std::pair<std::int64_t, std::int64_t>> cells;
	for (const auto &focus : foci)
	{
		const auto minx = floor_cell(focus.position.x - focus.radius, spacing);
		const auto maxx = floor_cell(focus.position.x + focus.radius, spacing);
		const auto miny = floor_cell(focus.position.y - focus.radius, spacing);
		const auto maxy = floor_cell(focus.position.y + focus.radius, spacing);
		if (maxx - minx > 512 || maxy - miny > 512)
			throw std::runtime_error("Asteroid streaming window exceeds 512 cells: increase lattice spacing");
		for (auto x = minx; x <= maxx; ++x)
			for (auto y = miny; y <= maxy; ++y)
			{
				cells.emplace(x, y);
				if (cells.size() > 65536)
					throw std::runtime_error("Asteroid fleet streaming exceeds 65536 cells");
			}
	}
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
	for (const auto &[x, y] : cells)
	{
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
}
Json save_fields() const
{
	Json out = Json::array();
	for (const auto &[id, r] : field_rocks)
	{
		auto p = fixed::Vec{r.scene.x, r.scene.y}, v = r.velocity, remainder = r.remainder;
		auto heading = r.scene.heading;
		if (r.loaded && !r.decorative)
		{
			const auto e = entities.at(id);
			p = e.get<Pose>().position;
			heading = e.get<Pose>().heading;
			v = e.get<Motion>().velocity;
			remainder = e.get<Motion>().remainder;
		}
		out.push_back({{"id", id},
		               {"definition", r.scene.definition},
		               {"x", p.x},
		               {"y", p.y},
		               {"vx", v.x},
		               {"vy", v.y},
		               {"rx", remainder.x},
		               {"ry", remainder.y},
		               {"heading", heading},
		               {"entry", r.scene.sprite_entry},
		               {"height", r.scene.visual_height_mm},
		               {"rotation", r.scene.rotation_mdeg},
		               {"spin", r.scene.spin_mdeg_s},
		               {"decorative", r.decorative},
		               {"loaded", r.loaded}});
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
	for (const auto &v : saved)
	{
		FieldRock r;
		auto &s = r.scene;
		s.id = v.at("id");
		s.definition = v.at("definition");
		s.x = v.at("x");
		s.y = v.at("y");
		s.heading = v.at("heading");
		s.sprite_entry = v.at("entry");
		s.visual_height_mm = v.at("height");
		s.rotation_mdeg = v.at("rotation");
		s.spin_mdeg_s = v.at("spin");
		s.field_instance = true;
		r.velocity = {v.at("vx"), v.at("vy")};
		r.remainder = {v.at("rx"), v.at("ry")};
		r.decorative = v.at("decorative");
		bool allowed = false;
		for (const auto &region : regions)
			if (region.asteroids)
			{
				const auto &types = r.decorative ? region.asteroids->decorative_types : region.asteroids->types;
				for (const auto &type : types)
					if (type.definition == s.definition)
						allowed = true;
			}
		const auto bounded = [](std::int64_t v, std::int64_t limit)
		{
			return v >= -limit && v <= limit;
		};
		if (!allowed || s.id >= (EntityId{1} << 53) || (s.id & 3) == 3 || r.decorative != ((s.id & 3) != 0) ||
		    s.heading < 0 || s.heading >= 360000 || !bounded(s.visual_height_mm, scenario_coordinate_limit) ||
		    !bounded(r.velocity.x, scenario_coordinate_limit) || !bounded(r.velocity.y, scenario_coordinate_limit) ||
		    !bounded(r.remainder.x, rate - 1) || !bounded(r.remainder.y, rate - 1) || s.id < field_identity_base ||
		    !definitions.contains(s.definition) ||
		    s.sprite_entry >= definitions.at(s.definition).sprite_entries.size() ||
		    !bounded(s.x, scenario_coordinate_limit) || !bounded(s.y, scenario_coordinate_limit) ||
		    s.rotation_mdeg < 0 || s.rotation_mdeg >= 360000 || !bounded(s.spin_mdeg_s, 360000) ||
		    field_rocks.contains(s.id))
			throw std::runtime_error("Invalid saved asteroid instance");
		auto &inserted = field_rocks.emplace(s.id, std::move(r)).first->second;
		if (v.at("loaded").get<bool>())
			load_rock(inserted);
	}
}
