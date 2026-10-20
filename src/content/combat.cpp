module;
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.content;
namespace phoenix
{
std::optional<CombatDefinition> compile_combat(const Catalog &catalog, const std::string &name)
{
	const auto &entity = catalog.entities.at(name);
	if (catalog.presentation)
		return {};
	if (!entity.contains("combat"))
	{
		for (const auto &fit : resolve_systems(catalog, name))
			if (fit.kind >= OperatingSystem::Weapon)
				throw std::runtime_error("entities." + name + ".combat: required for combat equipment");
		return {};
	}
	const auto path = "entities." + name + ".combat";
	const auto fail = [&](const std::string &why)
	{
		throw std::runtime_error(path + ": " + why);
	};
	const auto amount = [&](const Json &j, const char *key, std::int64_t fallback, std::int64_t low = 0,
	                        std::int64_t high = 1000000000LL)
	{
		const auto v = j.value(key, Json(fallback));
		if (!v.is_number_integer() || v < low || v > high)
			fail(std::string(key) + ": invalid bounded integer");
		return v.get<std::int64_t>();
	};
	CombatDefinition d;
	const auto &c = entity.at("combat");
	d.repair_console = c.value("repair_console", std::string{});
	if (!d.repair_console.empty())
	{
		const auto seats = resolve_consoles(catalog, name);
		if (std::none_of(seats.begin(), seats.end(),
		                 [&](const auto &s)
		                 {
			                 return s.id == d.repair_console;
		                 }))
			fail("unknown repair_console");
	}
	const auto &decks = c.at("decks");
	if (!decks.is_array() || decks.empty() || decks.size() > 16)
		fail("expected 1..16 decks");
	std::set<std::string> deck_ids;
	for (const auto &deck : decks)
	{
		const std::string id = deck.at("id");
		if (!deck_ids.insert(id).second)
			fail("duplicate deck " + id);
		const auto weight = amount(deck, "weight", 1, 1, 1000000);
		for (const auto &v : deck.at("compartments"))
		{
			CompartmentDefinition b;
			b.id = v.at("id");
			b.deck = id;
			b.deck_weight = weight;
			if (!valid_identifier(b.id) || !d.compartment_ids.emplace(b.id, d.compartments.size()).second)
				fail("invalid/duplicate compartment " + b.id);
			b.x = amount(v, "x_mm", 0, -1000000, 1000000);
			b.y = amount(v, "y_mm", 0, -1000000, 1000000);
			b.half_width = amount(v, "half_width_mm", 0, 1, 1000000);
			b.half_length = amount(v, "half_length_mm", 0, 1, 1000000);
			b.maximum = amount(v, "hp", 0, 2, 1000000);
			b.initial = amount(v, "initial_hp", b.maximum, 0, b.maximum);
			b.resistance = amount(v, "resistance_per_m", 0, 1, 1000000);
			b.recovery = amount(v, "recovery_hp", std::max<std::int64_t>(1, b.maximum / 5), 1, b.maximum - 1);
			for (const auto &other : d.compartments)
				if (other.deck == b.deck && std::abs(other.x - b.x) < other.half_width + b.half_width &&
				    std::abs(other.y - b.y) < other.half_length + b.half_length)
					fail("overlapping compartments " + other.id + " and " + b.id);
			d.compartments.push_back(b);
		}
	}
	if (d.compartments.empty() || d.compartments.size() > 256)
		fail("expected 1..256 compartments");
	std::set<std::pair<std::size_t, std::size_t>> routes;
	for (const auto &v : c.value("connections", Json::array()))
	{
		const std::string a = v.at("from"), b = v.at("to");
		if (!d.compartment_ids.contains(a) || !d.compartment_ids.contains(b) || a == b)
			fail("invalid route endpoints");
		if (!routes
		         .emplace(std::min(d.compartment_ids.at(a), d.compartment_ids.at(b)),
		                  std::max(d.compartment_ids.at(a), d.compartment_ids.at(b)))
		         .second)
			fail("duplicate undirected repair connection");
		d.connections.push_back(
		    {d.compartment_ids.at(a), d.compartment_ids.at(b), amount(v, "travel_ticks", 20, 1, 1000000)});
	}
	for (const auto &fit : resolve_systems(catalog, name))
	{
		const auto &v = entity.at("systems").at(fit.id);
		const auto compartment = v.value("compartment", std::string{});
		if (!d.compartment_ids.contains(compartment))
			fail("System " + fit.id + ": missing/unknown physical compartment");
		d.equipment_compartments[fit.id] = compartment;
		if (fit.kind < OperatingSystem::Weapon)
			continue;
		CombatSystemDefinition s;
		s.id = fit.id;
		s.kind = fit.kind;
		s.compartment = compartment;
		s.x = amount(v, "x_mm", 0, -1000000, 1000000);
		s.y = amount(v, "y_mm", 0, -1000000, 1000000);
		s.bearing = amount(v, "bearing_mdeg", 0, 0, 359999);
		s.arc = amount(v, "arc_mdeg", 360000, 1, 360000);
		if (s.kind == OperatingSystem::Weapon)
		{
			s.magazine = v.at("magazine");
			s.turret = v.value("turret", std::string{});
			s.interval = amount(v, "fire_ticks", 20, 1, 1000000);
			s.ammo_use = amount(v, "ammo_use", 1, 1, 1000000);
			const auto &p = v.at("projectile");
			s.projectile.visual = p.at("visual");
			s.projectile.speed = amount(p, "speed_mm_s", 0, 1, 10000000);
			s.projectile.lifetime = amount(p, "lifetime_ticks", 200, 1, 100000);
			s.projectile.penetration = amount(p, "penetration", 0, 1, 1000000);
			s.projectile.damage = amount(p, "damage", 0, 1, 1000000);
			s.projectile.deposition = amount(p, "damage_per_m", 0, 1, 1000000);
			s.projectile.spread = amount(p, "spread_mdeg", 0, 0, 45000);
			if (!catalog.entities.contains(s.projectile.visual))
				fail("unknown projectile visual " + s.projectile.visual);
		}
		if (s.kind == OperatingSystem::Turret)
		{
			s.traverse = amount(v, "traverse_mdeg_s", 0, 1, 360000);
			s.marker = v.at("marker");
			s.visual = v.at("visual");
			s.scale_permille = amount(v, "scale_permille", 1000, 1, 10000);
			s.flip_y = v.value("flip_y", false);
			if (!catalog.entities.contains(s.visual))
				fail("unknown turret visual " + s.visual);
			const auto material = compile_sprite(entity.at("render").at("sprite"), catalog, path);
			const auto check_frames = [&](const auto &frames)
			{
				for (const auto &frame : frames)
					if (!frame.attachments.contains(s.marker))
						fail("missing turret marker " + s.marker);
			};
			check_frames(material.frames);
			for (const auto &entry : material.entries)
				check_frames(entry.frames);
		}
		if (s.kind == OperatingSystem::Magazine)
		{
			s.capacity = amount(v, "capacity", 0, 1, 1000000);
			s.initial = amount(v, "initial", s.capacity, 0, s.capacity);
			const auto reload = v.value("reload", Json{{"strategy", "none"}});
			s.reload = reload.value("strategy", std::string("none"));
			if (s.reload != "none" && s.reload != "infinite" && s.reload != "energy")
				fail("unsupported reload strategy " + s.reload);
			s.refill_ticks = amount(reload, "ticks", 20, 1, 1000000);
			s.refill_amount = amount(reload, "amount", s.capacity, 1, s.capacity);
			s.reload_energy = amount(reload, "energy_per_second", 0, 0, 1000000000);
			if (s.reload == "energy" && (s.reload_energy == 0 || !v.contains("power_store")))
				fail("energy reload requires power_store and positive energy_per_second");
		}
		if (s.kind == OperatingSystem::RepairTeam)
		{
			s.repair_rate = amount(v, "hp_per_second", 10, 1, 1000000);
			s.stack_penalty = amount(v, "stack_penalty_permille", 500, 1, 10000);
		}
		d.system_ids.emplace(s.id, d.systems.size());
		d.systems.push_back(s);
	}
	for (const auto &s : d.systems)
		if (s.kind == OperatingSystem::Weapon)
		{
			const auto check = [&](const std::string &id, OperatingSystem kind)
			{
				if (!d.system_ids.contains(id) || d.systems[d.system_ids.at(id)].kind != kind)
					fail(s.id + ": incompatible reference " + id);
			};
			check(s.magazine, OperatingSystem::Magazine);
			if (!s.turret.empty())
				check(s.turret, OperatingSystem::Turret);
			if (s.ammo_use > d.systems[d.system_ids.at(s.magazine)].capacity)
				fail(s.id + ": shot exceeds magazine capacity");
		}
	const auto destruction = c.at("destruction");
	d.explosion = destruction.at("visual");
	d.explosion_sound = destruction.at("sound");
	if (!catalog.entities.contains(d.explosion) || !catalog.assets.contains(d.explosion_sound))
		fail("unresolved destruction asset");
	d.growth_ticks = amount(destruction, "growth_ticks", 10, 1, 10000);
	d.fade_ticks = amount(destruction, "fade_ticks", 20, 1, 10000);
	d.explosion_size = amount(destruction, "size_mm", 100000, 1, 1000000);
	d.collision_scale = amount(c, "collision_scale", 1, 1, 100000);
	d.collision_threshold = amount(c, "collision_threshold_mm_s", 1000, 1, 1000000);
	return d;
}
} // namespace phoenix
