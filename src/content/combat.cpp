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
DestructionDefinition compile_destruction(const Json &v, const Catalog &catalog, const std::string &path)
{
	DestructionDefinition d;
	const auto n = [&](const char *key, std::int64_t fallback, std::int64_t high)
	{
		const auto x = v.value(key, Json(fallback));
		if (!x.is_number_integer() || x < 0 || x > high)
			throw std::runtime_error(path + ": invalid " + key);
		return x.get<std::int64_t>();
	};
	d.visual = v.value("visual", std::string{});
	d.sound = v.value("sound", std::string{});
	if ((!d.visual.empty() && !catalog.entities.contains(d.visual)) ||
	    (!d.sound.empty() && !catalog.assets.contains(d.sound)))
		throw std::runtime_error(path + ": unresolved destruction asset");
	d.chance = n("chance_permille", 1000, 1000);
	d.damage = n("damage", 0, 1000000);
	d.radius = n("radius_mm", 0, 10000000);
	d.shield_bypass = n("shield_bypass_permille", 0, 1000);
	d.growth_ticks = n("growth_ticks", 1, 10000);
	d.fade_ticks = n("fade_ticks", 10, 10000);
	d.size = n("size_mm", 10000, 10000000);
	d.affects_missiles = v.value("affects_missiles", true);
	if ((d.damage && !d.radius) || !d.growth_ticks || !d.fade_ticks || !d.size)
		throw std::runtime_error(path + ": invalid destruction geometry/timing");
	return d;
}
std::optional<CombatDefinition> compile_combat(const Catalog &catalog, const std::string &name)
{
	const auto &entity = catalog.entities.at(name);
	if (catalog.presentation)
		return {};
	if (!entity.contains("combat"))
	{
		for (const auto &fit : resolve_systems(catalog, name))
			if (fit.kind >= OperatingSystem::Weapon && fit.kind <= OperatingSystem::Shield)
				throw std::runtime_error("entities." + name + ".combat: required for combat equipment");
		return {};
	}
	const auto path = "entities." + name + ".combat";
	SpriteCompiler sprites(catalog);
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
	d.half_height = amount(c, "half_height_mm", 5000, 1, 1000000);
	d.access_controller =
	    c.value("access_controller", entity.value("logistics", Json::object()).value("controller", std::string{}));
	if (!d.access_controller.empty())
	{
		bool found = false;
		for (const auto &ctrl : entity.at("control_layout").at("controllers"))
			found |= ctrl.at("id") == d.access_controller;
		if (!found)
			fail("unknown access_controller");
	}
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
	const auto projectile = [&](const Json &p)
	{
		ProjectileDefinition r;
		r.visual = p.at("visual");
		r.speed = amount(p, "speed_mm_s", 0, 1, 10000000);
		r.lifetime = amount(p, "lifetime_ticks", 200, 1, 100000);
		r.penetration = amount(p, "penetration", 1, 1, 1000000);
		r.damage = amount(p, "damage", 1, 1, 1000000);
		r.deposition = amount(p, "damage_per_m", 1, 1, 1000000);
		r.spread = amount(p, "spread_mdeg", 0, 0, 45000);
		r.shield_bypass = amount(p, "shield_bypass_permille", 0, 0, 1000);
		r.guidance = p.value("guidance", std::string("none"));
		if (r.guidance != "none" && r.guidance != "homing" && r.guidance != "chaotic")
			fail("unsupported projectile guidance");
		r.turn_rate = amount(p, "turn_mdeg_s", 90000, 1, 360000);
		r.seeker_range = amount(p, "seeker_range_mm", 1000000, 1, 10000000);
		r.arc_height = amount(p, "arc_height_mm", 0, 0, 1000000);
		r.chaos = amount(p, "chaos_mdeg", 0, 0, 90000);
		r.durability = amount(p, "durability", 0, 0, 1000000);
		r.radius = amount(p, "radius_mm", 100, 1, 100000);
		r.resistance = amount(p, "resistance_per_m", 1, 1, 1000000);
		r.blast_radius = amount(p, "blast_radius_mm", 0, 0, 10000000);
		r.fuse_radius = amount(p, "fuse_radius_mm", 0, 0, 1000000);
		r.trail = p.value("trail", std::string{});
		r.tracer = p.value("tracer", false);
		if (!catalog.entities.contains(r.visual) ||
		    (!r.trail.empty() && r.trail != "smoke" && !catalog.entities.contains(r.trail)))
			fail("unknown projectile/trail visual");
		if (p.contains("destruction"))
			r.destruction = compile_destruction(p.at("destruction"), catalog, path + ".projectile.destruction");
		if (r.durability && (r.destruction.shield_bypass || r.destruction.affects_missiles && r.destruction.damage))
			fail("missile destruction damage must have no shield bypass and exclude missiles");
		return r;
	};
	for (const auto &fit : resolve_systems(catalog, name))
	{
		const auto &v = entity.at("systems").at(fit.id);
		const auto compartment = v.value("compartment", std::string{});
		if (!d.compartment_ids.contains(compartment))
			fail("System " + fit.id + ": missing/unknown physical compartment");
		d.equipment_compartments[fit.id] = compartment;
		for (const auto &f : v.value("faults", Json::array()))
		{
			FaultDefinition fault;
			fault.id = f.at("id");
			fault.symptom = f.at("symptom");
			fault.diagnosis = f.at("diagnosis");
			fault.effect = f.value("effect", std::string("output"));
			if (fault.effect == "output" &&
			    (fit.kind == OperatingSystem::Generator || fit.kind == OperatingSystem::Storage ||
			     fit.kind == OperatingSystem::Distribution || fit.kind == OperatingSystem::Coolant ||
			     fit.kind == OperatingSystem::Magazine))
				fail("this mechanism requires a feed/disabled fault effect");
			if (!valid_identifier(fault.id) ||
			    (fault.effect != "output" && fault.effect != "disabled" && fault.effect != "feed"))
				fail("invalid fault identity/effect");
			for (const auto &existing : d.faults[fit.id])
				if (existing.id == fault.id)
					fail("duplicate fault");
			fault.chance = amount(f, "chance_permille", 100, 0, 1000);
			fault.threshold = amount(f, "damage_threshold", 1, 1, 1000000);
			fault.severity = amount(f, "severity", 1, 1, 1000);
			fault.effectiveness = amount(f, "effectiveness_permille", 500, 0, 1000);
			fault.repair_work = amount(f, "repair_work", 100, 1, 1000000);
			fault.destroys = f.value("destroyed", false);
			d.faults[fit.id].push_back(fault);
		}
		if (fit.kind < OperatingSystem::Weapon || fit.kind > OperatingSystem::Shield)
			continue;
		CombatSystemDefinition s(fit.kind);
		s.id = fit.id;
		s.compartment = compartment;
		s.x = amount(v, "x_mm", 0, -1000000, 1000000);
		s.y = amount(v, "y_mm", 0, -1000000, 1000000);
		s.bearing = amount(v, "bearing_mdeg", 0, 0, 359999);
		s.arc = amount(v, "arc_mdeg", 360000, 1, 360000);
		if (s.kind == OperatingSystem::Weapon)
		{
			s.get<WeaponDefinition>().family = v.value("family", std::string("projectile"));
			if (s.get<WeaponDefinition>().family != "projectile" && s.get<WeaponDefinition>().family != "beam")
				fail("unsupported weapon family");
			const auto magazine = v.value("magazine", std::string{});
			s.get<WeaponDefinition>().magazines = v.value("magazines", std::vector<std::string>{});
			if (s.get<WeaponDefinition>().magazines.empty() && !magazine.empty())
				s.get<WeaponDefinition>().magazines.push_back(magazine);
			if (s.get<WeaponDefinition>().family == "projectile" && s.get<WeaponDefinition>().magazines.empty())
				fail("projectile weapon requires magazines");
			s.get<WeaponDefinition>().turret = v.value("turret", std::string{});
			s.get<WeaponDefinition>().muzzle_marker = v.value("muzzle_marker", std::string{});
			s.ammunition().interval = amount(v, "fire_ticks", 20, 1, 1000000);
			s.get<WeaponDefinition>().ammo_use = amount(v, "ammo_use", 1, 1, 1000000);
			s.ammunition().load_ticks = amount(v, "load_ticks", 0, 0, 1000000);
			s.get<WeaponDefinition>().range = amount(v, "range_mm", 1000000, 1, 10000000);
			s.get<WeaponDefinition>().beam_damage = amount(v, "damage_per_second", 1, 1, 1000000);
			s.get<WeaponDefinition>().standby_energy = amount(v, "auto_energy_per_second", 0, 0, 1000000000);
			if (s.get<WeaponDefinition>().family == "beam" &&
			    (!v.contains("power_store") || amount(v, "energy_per_second", 0) == 0))
				fail("beam requires positive operating energy and power_store");
			s.get<WeaponDefinition>().point_defence = v.value("point_defence", false);
			if (s.get<WeaponDefinition>().point_defence &&
			    (!v.contains("power_store") || !s.get<WeaponDefinition>().standby_energy))
				fail("point defence requires standby energy and power_store");
			s.get<WeaponDefinition>().intercept_classes =
			    v.value("intercept_classes", std::vector<std::string>{"missile"});
			for (const auto &tag : s.get<WeaponDefinition>().intercept_classes)
				if (tag != "missile")
					fail("unsupported intercept class");
			if (v.contains("projectile"))
				s.ammunition().projectile = projectile(v.at("projectile"));
			else if (s.get<WeaponDefinition>().family == "beam")
			{
				s.ammunition().projectile.speed = 10000000;
				s.ammunition().projectile.penetration = amount(v, "penetration", 1000, 1, 1000000);
				s.ammunition().projectile.deposition = amount(v, "damage_per_m", 1000000, 1, 1000000);
				s.ammunition().projectile.shield_bypass = amount(v, "shield_bypass_permille", 0, 0, 1000);
			}
		}
		if (s.kind == OperatingSystem::Shield)
		{
			s.reserve().capacity = amount(v, "capacity", 0, 1, 1000000);
			s.reserve().initial = amount(v, "initial", s.reserve().capacity, 0, s.reserve().capacity);
			s.get<ShieldDefinition>().sustain_energy = amount(v, "sustain_energy_per_second", 1, 1, 1000000000);
			s.get<ShieldDefinition>().recharge_energy = amount(v, "energy_per_strength", 1, 1, 1000000);
			s.get<ShieldDefinition>().recharge_rate = amount(v, "recharge_per_second", 1, 1, 1000000);
			s.get<ShieldDefinition>().off_recharge_rate =
			    amount(v, "off_recharge_per_second", 1, 0, s.get<ShieldDefinition>().recharge_rate);
			s.get<ShieldDefinition>().warmup_ticks = amount(v, "warmup_ticks", 20, 1, 1000000);
			s.get<ShieldDefinition>().layer = amount(v, "layer", 0, 0, 1000000);
			s.get<ShieldDefinition>().discharge_rate = amount(v, "discharge_per_second", 1, 1, 1000000);
			s.get<ShieldDefinition>().discharge_efficiency = amount(v, "discharge_efficiency_permille", 1000, 1, 1000);
			if (!v.contains("power_store"))
				fail("shield requires power_store");
		}
		if (s.kind == OperatingSystem::Turret)
		{
			s.get<TurretDefinition>().traverse = amount(v, "traverse_mdeg_s", 0, 1, 360000);
			s.get<TurretDefinition>().marker = v.at("marker");
			s.get<TurretDefinition>().visual = v.at("visual");
			s.get<TurretDefinition>().scale_permille = amount(v, "scale_permille", 1000, 1, 10000);
			s.get<TurretDefinition>().flip_y = v.value("flip_y", false);
			if (!catalog.entities.contains(s.get<TurretDefinition>().visual))
				fail("unknown turret visual " + s.get<TurretDefinition>().visual);
			const auto material = sprites.compile(entity.at("render").at("sprite"), path);
			const auto check_frames = [&](const auto &frames)
			{
				for (const auto &frame : frames)
					if (!frame.attachments.contains(s.get<TurretDefinition>().marker))
						fail("missing turret marker " + s.get<TurretDefinition>().marker);
			};
			check_frames(material.frames);
			for (const auto &entry : material.entries)
				check_frames(entry.frames);
		}
		if (s.kind == OperatingSystem::Magazine)
		{
			s.ammunition().interval = amount(v, "fire_ticks", 0, 0, 1000000);
			s.ammunition().load_ticks = amount(v, "load_ticks", 0, 0, 1000000);
			if (v.contains("projectile"))
				s.ammunition().projectile = projectile(v.at("projectile"));
			s.reserve().capacity = amount(v, "capacity", 0, 1, 1000000);
			s.reserve().initial = amount(v, "initial", s.reserve().capacity, 0, s.reserve().capacity);
			const auto reload = v.value("reload", Json{{"strategy", "none"}});
			s.get<MagazineDefinition>().reload = reload.value("strategy", std::string("none"));
			if (s.get<MagazineDefinition>().reload != "none" && s.get<MagazineDefinition>().reload != "infinite" &&
			    s.get<MagazineDefinition>().reload != "energy")
				fail("unsupported reload strategy " + s.get<MagazineDefinition>().reload);
			s.get<MagazineDefinition>().refill_ticks = amount(reload, "ticks", 20, 1, 1000000);
			s.get<MagazineDefinition>().refill_amount =
			    amount(reload, "amount", s.reserve().capacity, 1, s.reserve().capacity);
			s.get<MagazineDefinition>().reload_energy = amount(reload, "energy_per_second", 0, 0, 1000000000);
			if (s.get<MagazineDefinition>().reload == "energy" &&
			    (s.get<MagazineDefinition>().reload_energy == 0 || !v.contains("power_store")))
				fail("energy reload requires power_store and positive energy_per_second");
		}
		if (s.kind == OperatingSystem::Team)
		{
			s.get<TeamDefinition>().repair_rate = amount(v, "hp_per_second", 10, 1, 1000000);
			s.get<TeamDefinition>().stack_penalty = amount(v, "stack_penalty_permille", 500, 1, 10000);
			auto &team = s.get<TeamDefinition>();
			team.type = v.value("type", std::string{"engineering"});
			if (team.type.empty() || team.type.size() > 64)
				fail("invalid team type");
			team.capabilities = v.value("capabilities", std::set<std::string>{"repair"});
			if (team.capabilities.size() > 32)
				fail("too many team capabilities");
			for (const auto &c : team.capabilities)
				if (c.empty() || c.size() > 64)
					fail("invalid team capability");
			team.treatment_ticks = amount(v, "treatment_ticks", 400, 1, 1728000);
			team.stabilise_ticks = amount(v, "stabilise_ticks", 200, 1, 1728000);
			team.injured_effectiveness = amount(v, "injured_effectiveness_permille", 500, 0, 1000);
			std::set<std::string> orders;
			for (const auto &o : v.value("orders", Json::array()))
			{
				TeamOrderDefinition order;
				order.id = o.at("id");
				order.label = o.at("label");
				order.icon = o.value("icon", std::string{"team"});
				order.target = o.value("target", std::string{"compartments"});
				order.capability = o.value("capability", std::string{});
				if (order.id.empty() || order.id.size() > 64 ||
				    order.id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") !=
				        std::string::npos ||
				    !orders.insert(order.id).second || order.label.empty() || order.label.size() > 128 ||
				    order.icon.size() > 64)
					fail("invalid team order");
				if (order.target != "compartments" && order.target != "damage" && order.target != "patients")
					fail("invalid team target filter");
				if (!order.capability.empty() && !team.capabilities.contains(order.capability))
					fail("order requires absent team capability");
				const auto key = o.at("tree").get<std::string>();
				if (!catalog.tree_programs.contains(key) ||
				    catalog.tree_programs.at(key).context != state_tree::Context::Team)
					fail("order requires team tree");
				order.patient_states =
				    o.value("patient_states", std::set<std::string>{"minor", "severe", "stabilised"});
				if (order.patient_states.size() > 3)
					fail("invalid patient filter");
				for (const auto &state : order.patient_states)
					if (state != "minor" && state != "severe" && state != "stabilised")
						fail("invalid patient condition filter");
				order.program = catalog.tree_programs.at(key);
				const auto validate = [&](const auto &self, const state_tree::Program &p) -> void
				{
					for (const auto &state : p.states)
						for (const auto *tasks : {&state.entry, &state.running, &state.exit})
							for (const auto &task : *tasks)
								if (task.kind == state_tree::TaskKind::Action)
								{
									const auto &a = p.registry.actions.at(task.target).name;
									if (a == "repair" && !team.capabilities.contains("repair"))
										fail("repair order requires capability");
									if ((a == "treat" || a == "stabilise" || a == "recover") &&
									    !team.capabilities.contains("medical"))
										fail("medical order requires capability");
								}
					for (const auto &ref : p.references)
						self(self, ref);
				};
				validate(validate, order.program);
				team.orders.push_back(std::move(order));
			}
			if (team.orders.size() > 32)
				fail("too many team orders");
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
			std::set<std::string> seen_magazines;
			for (const auto &mag : s.get<WeaponDefinition>().magazines)
			{
				check(mag, OperatingSystem::Magazine);
				if (!seen_magazines.insert(mag).second ||
				    s.get<WeaponDefinition>().ammo_use > d.systems[d.system_ids.at(mag)].reserve().capacity)
					fail(s.id + ": invalid magazine capacity/duplicate");
				if (s.ammunition().projectile.visual.empty() &&
				    d.systems[d.system_ids.at(mag)].ammunition().projectile.visual.empty())
					fail(s.id + ": missing ammunition projectile");
			}
			if (!s.get<WeaponDefinition>().turret.empty())
				check(s.get<WeaponDefinition>().turret, OperatingSystem::Turret);
			const auto &weapon = s.get<WeaponDefinition>();
			if (!weapon.muzzle_marker.empty())
			{
				if (!valid_identifier(weapon.muzzle_marker))
					fail("invalid muzzle marker");
				const auto &visual =
				    weapon.turret.empty()
				        ? entity
				        : catalog.entities.at(
				              d.systems.at(d.system_ids.at(weapon.turret)).get<TurretDefinition>().visual);
				const auto sprite = sprites.compile(visual.at("render").at("sprite"), path);
				const auto verify = [&](const auto &frames)
				{
					for (const auto &frame : frames)
						if (!frame.attachments.contains(weapon.muzzle_marker))
							fail("missing muzzle marker " + weapon.muzzle_marker);
				};
				verify(sprite.frames);
				for (const auto &entry : sprite.entries)
					verify(entry.frames);
			}
		}
	auto destruction = c.at("destruction");
	// Preserve ship defaults while compiling a single shared destruction definition.
	const Json defaults{{"growth_ticks", 10}, {"fade_ticks", 20}, {"size_mm", 100000}};
	for (const auto &[key, fallback] : defaults.items())
		if (!destruction.contains(key))
			destruction[key] = fallback;
	d.destruction = compile_destruction(destruction, catalog, path + ".destruction");
	d.collision_scale = amount(c, "collision_scale", 1, 1, 100000);
	d.collision_threshold = amount(c, "collision_threshold_mm_s", 1000, 1, 1000000);
	return d;
}
} // namespace phoenix
