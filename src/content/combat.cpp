module;
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.content;
namespace phoenix
{
const std::vector<ModifierPropertyDescriptor> &modifier_properties()
{
	static const std::vector<ModifierPropertyDescriptor> properties{
	    {"propulsion",
	     "mm_per_second_squared",
	     {OperatingSystem::Engine, OperatingSystem::Lateral, OperatingSystem::Boost, OperatingSystem::Impulse}},
	    {"speed", "mm_per_second", {OperatingSystem::Engine, OperatingSystem::Boost, OperatingSystem::Impulse}},
	    {"turn", "mdeg_per_second", {OperatingSystem::Steering, OperatingSystem::Engine}, 360000},
	    {"sensors", "mm", {OperatingSystem::Radar}},
	    {"cycling", "relative_permille", {OperatingSystem::Weapon}},
	    {"traverse", "mdeg_per_second", {OperatingSystem::Turret}, 360000},
	    {"reload", "relative_permille", {OperatingSystem::Magazine}},
	    {"recharge", "milli_strength_per_second", {OperatingSystem::Shield}},
	    {"off_recharge", "milli_strength_per_second", {OperatingSystem::Shield}},
	    {"sustain", "energy_per_second", {OperatingSystem::Shield}},
	    {"discharge", "strength_per_second", {OperatingSystem::Shield}, 1000000},
	    {"protection", "relative_permille", {OperatingSystem::Shield}, 1000},
	    {"repair", "native_work_units", {OperatingSystem::Team}, 1000000000000000000LL},
	    {"medical", "relative_permille", {OperatingSystem::Team}},
	    {"security", "relative_permille", {OperatingSystem::Team}},
	    {"comms", "mm", {OperatingSystem::Comms}}};
	return properties;
}
bool known_modifier_property(std::string_view property)
{
	return std::any_of(modifier_properties().begin(), modifier_properties().end(),
	                   [&](const auto &d)
	                   {
		                   return d.id == property;
	                   });
}
bool property_compatible(std::string_view property, OperatingSystem kind)
{
	for (const auto &d : modifier_properties())
		if (d.id == property)
			return std::find(d.owners.begin(), d.owners.end(), kind) != d.owners.end();
	return false;
}
std::string modifier_property_role(std::string_view property)
{
	return std::string(property);
}
ModifierEffectDefinition compile_modifier_effect(const Catalog &catalog, const std::string &id)
{
	const auto path = "effects." + id;
	const auto found = catalog.effects.find(id);
	if (found == catalog.effects.end())
		throw std::runtime_error(path + ": unknown modifier effect");
	const auto &effect = found->second;
	if (effect.value("kind", std::string{}) != "modifier")
		throw std::runtime_error(path + ".kind: expected modifier effect");
	for (const auto &[key, value] : effect.items())
		if (key != "id" && key != "kind" && key != "contributions" && key != "extends" && key != "name" &&
		    key != "duration_ticks")
			throw std::runtime_error(path + "." + key + ": unsupported effect field");
	const auto duration = effect.value("duration_ticks", Json(0));
	if (!duration.is_number_integer() || duration < 0 || duration > 1000000)
		throw std::runtime_error(path + ".duration_ticks: expected bounded nonnegative integer");
	if (!effect.contains("contributions") || !effect.at("contributions").is_object() ||
	    effect.at("contributions").empty() || effect.at("contributions").size() > 64)
		throw std::runtime_error(path + ".contributions: expected 1..64 named contributions");
	ModifierEffectDefinition result{id, duration.get<std::int64_t>(), {}};
	for (const auto &[name, value] : effect.at("contributions").items())
	{
		const auto at = path + ".contributions." + name;
		if (!valid_identifier(name) || !value.is_object())
			throw std::runtime_error(at + ": invalid named contribution");
		for (const auto &[key, item] : value.items())
			if (key != "property" && key != "role" && key != "delta_permille")
				throw std::runtime_error(at + "." + key + ": unsupported modifier field");
		ModifierContributionDefinition c;
		c.name = name;
		c.property = value.value("property", std::string{});
		c.role = value.value("role", modifier_property_role(c.property));
		const auto delta = value.value("delta_permille", Json());
		if (!known_modifier_property(c.property) || !valid_identifier(c.role))
			throw std::runtime_error(at + ": unknown property or invalid target role");
		if (!delta.is_number_integer() || delta < -100000 || delta > 100000)
			throw std::runtime_error(at + ".delta_permille: expected integer in -100000..100000");
		c.delta_permille = delta.get<std::int64_t>();
		result.contributions.push_back(std::move(c));
	}
	return result;
}
void compile_effect(const Catalog &catalog, const std::string &id)
{
	const auto &effect = catalog.effects.at(id);
	if (effect.value("kind", std::string{}) == "modifier")
		(void)compile_modifier_effect(catalog, id);
	else
		(void)compile_shield_effect(catalog, id);
}
ShieldEffectDefinition compile_shield_effect(const Catalog &catalog, const std::string &id)
{
	const auto path = "effects." + id;
	const auto found = catalog.effects.find(id);
	if (found == catalog.effects.end())
		throw std::runtime_error(path + ": unknown effect reference");
	const auto &effect = found->second;
	if (effect.value("kind", std::string{}) != "shield")
		throw std::runtime_error(path + ".kind: unsupported effect kind");
	for (const auto &[key, value] : effect.items())
		if (key != "id" && key != "kind" && key != "contributions" && key != "extends" && key != "name" &&
		    key != "duration_ticks")
			throw std::runtime_error(path + "." + key + ": unsupported effect field");
	if (effect.contains("duration_ticks") && (!effect.at("duration_ticks").is_number_integer() ||
	                                          effect.at("duration_ticks") < 0 || effect.at("duration_ticks") > 1000000))
		throw std::runtime_error(path + ".duration_ticks: expected bounded nonnegative integer");
	if (!effect.contains("contributions") || !effect.at("contributions").is_object() ||
	    effect.at("contributions").empty() || effect.at("contributions").size() > 64)
		throw std::runtime_error(path + ".contributions: expected 1..64 named contributions");
	ShieldEffectDefinition result;
	result.duration_ticks = effect.value("duration_ticks", std::int64_t{0});
	for (const auto &[name, value] : effect.at("contributions").items())
	{
		const auto at = path + ".contributions." + name;
		if (!valid_identifier(name) || !value.is_object())
			throw std::runtime_error(at + ": invalid named contribution");
		static const std::set<std::string> fields{"capacity",
		                                          "initial",
		                                          "bearing_mdeg",
		                                          "arc_mdeg",
		                                          "sustain_energy_per_second",
		                                          "energy_per_strength",
		                                          "recharge_per_second",
		                                          "off_recharge_per_second",
		                                          "warmup_ticks",
		                                          "layer",
		                                          "discharge_per_second",
		                                          "discharge_efficiency_permille",
		                                          "pre_damage_permille",
		                                          "post_damage",
		                                          "reactions",
		                                          "modifiers"};
		for (const auto &[key, item] : value.items())
			if (!fields.contains(key))
				throw std::runtime_error(at + "." + key + ": unsupported shield field");
		auto n = [&](const char *key, std::int64_t fallback, std::int64_t minimum, std::int64_t maximum)
		{
			if (!value.contains(key))
				return fallback;
			const auto &v = value.at(key);
			if (!v.is_number_integer() || v.is_boolean() || v < minimum || v > maximum)
				throw std::runtime_error(at + "." + key + ": integer outside supported bounds");
			return v.get<std::int64_t>();
		};
		ShieldContributionDefinition c;
		c.name = name;
		c.bearing = n("bearing_mdeg", 0, 0, 359999);
		c.arc = n("arc_mdeg", 360000, 1, 360000);
		auto &d = c.shield;
		if (value.contains("modifiers"))
		{
			const auto &modifiers = value.at("modifiers");
			if (!modifiers.is_object())
				throw std::runtime_error(at + ".modifiers: expected object");
			for (const auto &[key, modifier] : modifiers.items())
			{
				if (!modifier.is_number_integer() || modifier < -1000 || modifier > 100000)
					throw std::runtime_error(at + ".modifiers." + key + ": invalid percentage delta");
				auto number = modifier.get<std::int64_t>();
				if (key == "recharge")
					d.recharge_modifier = number;
				else if (key == "sustain")
					d.sustain_modifier = number;
				else if (key == "discharge")
					d.discharge_modifier = number;
				else if (key == "protection")
					d.protection_modifier = number;
				else
					throw std::runtime_error(at + ".modifiers." + key + ": unsupported live property");
			}
		}

		if (value.contains("reactions"))
		{
			const auto &groups = value.at("reactions");
			if (!groups.is_array() || groups.size() > 32)
				throw std::runtime_error(at + ".reactions: expected bounded array");
			for (const auto &group : groups)
			{
				if (!group.is_object())
					throw std::runtime_error(at + ".reactions: expected group object");
				for (const auto &[key, field] : group.items())
					if (key != "payer" && key != "energy" && key != "operations")
						throw std::runtime_error(at + ".reactions: unsupported group field " + key);
				ShieldReactionDefinition reaction;
				reaction.payer = group.value("payer", std::string{});
				const auto energy = group.value("energy", Json(0));
				if (!energy.is_number_integer() || energy < 0 || energy > 1000000000)
					throw std::runtime_error(at + ".reactions.energy: invalid cost");
				reaction.energy = energy.get<std::int64_t>();
				if (reaction.energy && !valid_identifier(reaction.payer))
					throw std::runtime_error(at + ".reactions.payer: paid reaction requires equipment payer");
				if (!group.contains("operations") || !group.at("operations").is_array() ||
				    group.at("operations").empty() || group.at("operations").size() > 32)
					throw std::runtime_error(at + ".reactions.operations: expected 1..32 operations");
				bool damage_seen = false;
				for (const auto &operation : group.at("operations"))
				{
					if (!operation.is_object())
						throw std::runtime_error(at + ".reactions.operations: expected object");
					for (const auto &[key, field] : operation.items())
						if (key != "kind" && key != "target" && key != "damage" && key != "effect" && key != "grant" &&
						    key != "equipment")
							throw std::runtime_error(at + ".reactions.operations: unsupported field " + key);
					ShieldReactionOperation op;
					op.target = operation.value("target", std::string("attacker"));
					if (op.target != "attacker" && op.target != "protected")
						throw std::runtime_error(at + ".reactions.target: expected attacker/protected");
					const auto kind = operation.value("kind", std::string{});
					for (const auto &[key, field] : operation.items())
						if (key != "kind" && key != "target" &&
						    !((kind == "damage" && key == "damage") ||
						      (kind == "apply" && (key == "effect" || key == "grant" || key == "equipment")) ||
						      (kind == "remove" && key == "grant")))
							throw std::runtime_error(at + ".reactions.operations: unsupported field for " + kind +
							                         ": " + key);
					if (kind != "damage" && damage_seen)
						throw std::runtime_error(at + ".reactions.operations: grant changes must precede damage");
					if (kind == "damage")
						damage_seen = true;
					if (kind == "damage")
					{
						op.kind = ShieldReactionKind::Damage;
						const auto damage = operation.value("damage", Json(0));
						if (!damage.is_number_integer() || damage <= 0 || damage > 1000000)
							throw std::runtime_error(at + ".reactions.damage: expected bounded positive damage");
						op.damage = damage.get<std::int64_t>();
					}
					else if (kind == "apply" || kind == "remove")
					{
						op.kind = kind == "apply" ? ShieldReactionKind::Apply : ShieldReactionKind::Remove;
						op.grant = operation.value("grant", std::string{});
						if (!valid_identifier(op.grant))
							throw std::runtime_error(at + ".reactions.grant: invalid handle");
						if (kind == "apply")
						{
							op.effect = operation.value("effect", std::string{});
							if (!catalog.effects.contains(op.effect))
								throw std::runtime_error(at + ".reactions.effect: unknown effect");
							op.equipment = operation.value("equipment", std::string{});
							const bool modifier =
							    catalog.effects.at(op.effect).value("kind", std::string{}) == "modifier";
							if (modifier ? !valid_identifier(op.equipment) : !op.equipment.empty())
								throw std::runtime_error(
								    at +
								    ".reactions.equipment: modifier requires equipment binding; shield must omit it");
						}
					}
					else
						throw std::runtime_error(at + ".reactions.kind: unsupported operation");
					reaction.operations.push_back(std::move(op));
				}
				d.reactions.push_back(std::move(reaction));
			}
		}

		d.pre_damage_permille = n("pre_damage_permille", 1000, 0, 1000);
		if (const auto damage = n("post_damage", 0, 0, 1000000))
		{
			// Keep shorthand last: existing saves identify this group after authored reactions.
			ShieldReactionDefinition reaction;
			reaction.operations.push_back({ShieldReactionKind::Damage, "attacker", {}, {}, {}, damage});
			d.reactions.push_back(std::move(reaction));
		}
		d.reserve.capacity = n("capacity", 1, 1, 1000000);
		d.reserve.initial = n("initial", d.reserve.capacity, 0, d.reserve.capacity);
		d.sustain_energy = n("sustain_energy_per_second", 1, 0, 1000000000);
		d.recharge_energy = n("energy_per_strength", 1, 1, 1000000);
		d.recharge_rate = n("recharge_per_second", 1, 0, 1000000);
		d.off_recharge_rate = n("off_recharge_per_second", 1, 0, d.recharge_rate);
		if (d.off_recharge_rate > d.recharge_rate)
			throw std::runtime_error(at + ": off recharge exceeds recharge");
		d.warmup_ticks = n("warmup_ticks", 20, 1, 1000000);
		d.layer = n("layer", 0, 0, 1000000);
		d.discharge_rate = n("discharge_per_second", 1, 0, 1000000);
		d.discharge_efficiency = n("discharge_efficiency_permille", 1000, 1, 1000);
		result.contributions.push_back(std::move(c));
	}
	return result;
}
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
			if (v.contains("shield_effect"))
			{
				s.shield_effect = v.at("shield_effect").get<std::string>();
				s.shield_definition = compile_shield_effect(catalog, s.shield_effect);
				s.get<ShieldDefinition>() = s.shield_definition.contributions.front().shield;
				s.bearing = s.shield_definition.contributions.front().bearing;
				s.arc = s.shield_definition.contributions.front().arc;
			}
			else
				fail("shield requires catalogue shield_effect reference");

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
