module;
#include <utility>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.content.combat;
import phoenix.content.assets;
import phoenix.content.catalog;
import phoenix.content.combat.types;
import phoenix.content.controllers;
import phoenix.content.effects;
import phoenix.content.effects.types;
import phoenix.content.operations.types;
import phoenix.content.resources;
import phoenix.content.sensing;

namespace phoenix
{
ModifierEffectDefinition compile_modifier_effect(const Catalog &catalog, const std::string &id,
                                                 SharedPropertyCatalog compiled)
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
		    key != "duration_ticks" && key != "rules")
			throw std::runtime_error(path + "." + key + ": unsupported effect field");
	const auto duration = effect.value("duration_ticks", Json(0));
	if (!duration.is_number_integer() || duration < 0 || duration > 1000000)
		throw std::runtime_error(path + ".duration_ticks: expected bounded nonnegative integer");
	if (!effect.contains("contributions") || !effect.at("contributions").is_object() ||
	    effect.at("contributions").empty() || effect.at("contributions").size() > 64)
		throw std::runtime_error(path + ".contributions: expected 1..64 named contributions");
	const auto properties = compiled ? std::move(compiled) : compile_property_catalog(catalog);
	ModifierEffectDefinition result{id, duration.get<std::int64_t>(), {}};
	for (const auto &[name, value] : effect.at("contributions").items())
	{
		const auto at = path + ".contributions." + name;
		if (!valid_identifier(name) || !value.is_object())
			throw std::runtime_error(at + ": invalid named contribution");
		for (const auto &[key, item] : value.items())
			if (key != "property" && key != "role" && key != "delta_permille" && key != "value" && key != "priority")
				throw std::runtime_error(at + "." + key + ": unsupported modifier field");
		ModifierContributionDefinition c;
		c.name = name;
		c.property = value.value("property", std::string{});
		c.role = value.value("role", modifier_property_role(c.property));
		const auto delta = value.value("delta_permille", Json());
		const auto custom = properties->find(c.property);
		if ((!known_modifier_property(c.property) && custom == properties->end()) || !valid_identifier(c.role))
			throw std::runtime_error(at + ": unknown property or invalid target role");
		const bool override_value = custom != properties->end() && (custom->second.kind == CustomPropertyKind::Flag ||
		                                                            custom->second.kind == CustomPropertyKind::Enum);
		if (override_value)
		{
			if (value.contains("delta_permille"))
				throw std::runtime_error(at + ": flag/enum overrides omit percentage delta");
			c.value = read_custom_property_value(custom->second, value.at("value"), at + ".value");
			const auto priority = value.value("priority", Json(0));
			if (!priority.is_number_integer() || priority < -1000000 || priority > 1000000)
				throw std::runtime_error(at + ": invalid override priority");
			c.priority = priority.get<std::int64_t>();
		}
		else
		{
			if (value.contains("value") || value.contains("priority") ||
			    (custom != properties->end() && custom->second.kind != CustomPropertyKind::Attribute))
				throw std::runtime_error(at + ": only attributes accept percentage modifiers");
			if (!delta.is_number_integer() || delta < -100000 || delta > 100000)
				throw std::runtime_error(at + ".delta_permille: expected integer in -100000..100000");
			c.delta_permille = delta.get<std::int64_t>();
		}
		result.contributions.push_back(std::move(c));
	}
	return result;
}
void compile_effect(const Catalog &catalog, const std::string &id, SharedPropertyCatalog compiled)
{
	const auto properties = compiled ? std::move(compiled) : compile_property_catalog(catalog);
	const auto found = catalog.effects.find(id);
	if (found == catalog.effects.end())
		throw std::runtime_error("effects." + id + ": unknown effect reference");
	const auto rules = compile_bound_rules(catalog, found->second, properties);
	compile_effect(catalog, id, rules, properties);
}
void compile_effect(const Catalog &catalog, const std::string &id, const RulesCatalog &rules,
                    SharedPropertyCatalog compiled)
{
	const auto properties = compiled ? std::move(compiled) : compile_property_catalog(catalog);
	const auto &effect = catalog.effects.at(id);
	if (effect.value("kind", std::string{}) == "modifier")
		(void)compile_modifier_effect(catalog, id, properties);
	else if (effect.value("kind", std::string{}) == "operations")
		(void)compile_effect_group(catalog, id, properties);
	else if (effect.value("kind", std::string{}) == "directed_mode")
		(void)compile_directed_mode(catalog, id);
	else if (effect.value("kind", std::string{}) == "targeting_policy")
		(void)compile_targeting_policy(catalog, id);
	else if (effect.value("kind", std::string{}) == "payload")
		(void)compile_payload(catalog, id, rules);
	else if (effect.value("kind", std::string{}) == "periodic")
		(void)compile_periodic_effect(catalog, id, rules);
	else if (effect.value("kind", std::string{}) == "ability")
		(void)compile_ability(catalog, id, properties);
	else if (effect.value("kind", std::string{}) == "psychic")
		(void)compile_psychic_effect(catalog, id);
	else if (effect.value("kind", std::string{}) == "process")
		(void)compile_process(catalog, id, properties);
	else if (effect.value("kind", std::string{}) == "process_grant")
		(void)compile_process_grant(catalog, id);
	else if (effect.value("kind", std::string{}) == "ability_grant")
		(void)compile_ability_grant(catalog, id);
	else if (effect.value("kind", std::string{}) == "rule")
		(void)compile_rule(catalog, id, properties);
	else
		(void)compile_shield_effect(catalog, id, rules);
}
ShieldEffectDefinition compile_shield_effect(const Catalog &catalog, const std::string &id,
                                             SharedPropertyCatalog compiled)
{
	const auto found = catalog.effects.find(id);
	if (found == catalog.effects.end())
		throw std::runtime_error("effects." + id + ": unknown effect reference");
	const auto rules = compile_bound_rules(catalog, found->second, std::move(compiled));
	return compile_shield_effect(catalog, id, rules);
}
ShieldEffectDefinition compile_shield_effect(const Catalog &catalog, const std::string &id, const RulesCatalog &rules)
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
		    key != "duration_ticks" && key != "rules")
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
		                                          "rules",
		                                          "modifiers",
		                                          "audio"};
		for (const auto &[key, item] : value.items())
		{
			if (key == "pre_damage_permille" || key == "post_damage" || key == "reactions")
				throw std::runtime_error(at + "." + key + ": replaced by shared rule bindings in rules");
			if (!fields.contains(key))
				throw std::runtime_error(at + "." + key + ": unsupported shield field");
		}
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
		if (value.contains("audio"))
		{
			auto clip = value.at("audio");
			for (const auto *key : {"damage_threshold", "damage_full_spike"})
				if (clip.contains(key) && !clip.at(key).is_number_integer())
					throw std::runtime_error(at + ".audio." + key + ": expected integer");
			d.audio_peak_gain = clip.value("peak_gain", .8f);
			d.audio_decay = clip.value("decay_per_second", 1.5f);
			d.audio_threshold = clip.value("damage_threshold", std::int64_t{1});
			d.audio_full_damage = clip.value("damage_full_spike", std::int64_t{30});
			for (const auto *key : {"peak_gain", "decay_per_second", "damage_threshold", "damage_full_spike"})
				clip.erase(key);
			d.audio = compile_audio_clip(catalog, clip, at + ".audio");
			if (!std::isfinite(d.audio_peak_gain) || d.audio_peak_gain < d.audio.gain || d.audio_peak_gain > 1 ||
			    !std::isfinite(d.audio_decay) || d.audio_decay <= 0 || d.audio_decay > 100 || d.audio_threshold < 1 ||
			    d.audio_full_damage < d.audio_threshold || d.audio_full_damage > 1000000)
				throw std::runtime_error(at + ".audio: invalid shield envelope");
		}
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

		static constexpr std::array allowed{RuleTrigger::PreShieldDamage, RuleTrigger::PostShieldDamage};
		d.rules = compile_rule_bindings(rules, value.value("rules", Json::array()), at + ".rules", allowed);
		for (const auto &binding : d.rules)
			if (rules.at(binding.rule).phase != RulePhase::Instant)
				throw std::runtime_error(at + ".rules: damage hooks require instant phase");
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
	d.payloads = compile_payloads(catalog, v.value("payloads", Json::array()), path + ".payloads");
	if ((d.damage && !d.radius) || !d.growth_ticks || !d.fade_ticks || !d.size)
		throw std::runtime_error(path + ": invalid destruction geometry/timing");
	return d;
}
ProjectileDefinition compile_projectile_profile(const Catalog &catalog, const Json &p, const std::string &path)
{
	const auto fail = [&](const std::string &why)
	{
		throw std::runtime_error(path + ": " + why);
	};
	const auto amount = [&](const Json &j, const char *key, std::int64_t fallback, std::int64_t low, std::int64_t high)
	{
		const auto value = j.value(key, Json(fallback));
		if (!value.is_number_integer() || value < low || value > high)
			fail(std::string(key) + ": invalid bounded integer");
		return value.get<std::int64_t>();
	};
	ProjectileDefinition r;
	r.visual = p.at("visual");
	r.speed = amount(p, "speed_mm_s", 0, 1, 10000000);
	r.lifetime = amount(p, "lifetime_ticks", 200, 1, 100000);
	r.penetration = amount(p, "penetration", 1, 1, 1000000);
	r.damage = amount(p, "damage", 1, 0, 1000000);
	r.payloads = compile_payloads(catalog, p.value("payloads", Json::array()), path + ".payloads");
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
}
AmmunitionDefinition compile_ammunition_resource(const Catalog &catalog, const std::string &id)
{
	const auto path = "resources." + id + ".ammunition";
	const auto &resource = catalog.resources.at(id);
	AmmunitionDefinition result;
	result.interval = 0;
	if (!resource.contains("ammunition"))
		return result;
	const auto &value = resource.at("ammunition");
	if (!value.is_object())
		throw std::runtime_error(path + ": expected object");
	for (const auto &[key, field] : value.items())
		if (key != "fire_ticks" && key != "load_ticks" && key != "projectile")
			throw std::runtime_error(path + "." + key + ": unsupported ammunition field");
	for (const auto *key : {"fire_ticks", "load_ticks"})
	{
		const auto amount = value.value(key, Json(0));
		if (!amount.is_number_integer() || amount < 0 || amount > 1000000)
			throw std::runtime_error(path + "." + key + ": invalid bounded integer");
		(std::string(key) == "fire_ticks" ? result.interval : result.load_ticks) = amount.get<std::int64_t>();
	}
	if (value.contains("projectile"))
		result.projectile = compile_projectile_profile(catalog, value.at("projectile"), path);
	return result;
}
std::optional<CombatDefinition> compile_combat(const Catalog &catalog, const std::string &name,
                                               SharedPropertyCatalog compiled)
{
	const DirectedCatalog directed_catalog{{name, compile_directed(catalog, name)}};
	return compile_combat(catalog, name, directed_catalog, std::move(compiled));
}
std::optional<CombatDefinition> compile_combat(const Catalog &catalog, const std::string &name,
                                               const DirectedCatalog &directed_catalog, SharedPropertyCatalog compiled)
{
	const auto properties = compiled ? std::move(compiled) : compile_property_catalog(catalog);
	const auto rules = compile_entity_rule_scope(catalog, name, properties);
	return compile_combat(catalog, name, directed_catalog, rules);
}
std::optional<CombatDefinition> compile_combat(const Catalog &catalog, const std::string &name,
                                               const DirectedCatalog &directed_catalog, const RulesCatalog &rules)
{
	SharedPropertyCatalog team_effect_properties;
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
	const auto &directed = directed_catalog.at(name);
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
		return compile_projectile_profile(catalog, p, path + ".projectile");
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
		const bool directed_weapon =
		    directed.contains(fit.id) && std::any_of(directed.at(fit.id).modes.begin(), directed.at(fit.id).modes.end(),
		                                             [](const auto &m)
		                                             {
			                                             return m.operation != DirectedOperationKind::Scan;
		                                             });
		if (!directed_weapon && (fit.kind < OperatingSystem::Weapon || fit.kind > OperatingSystem::Shield))
			continue;
		CombatSystemDefinition s(directed_weapon ? OperatingSystem::Weapon : fit.kind);
		s.id = fit.id;
		s.compartment = compartment;
		s.x = amount(v, "x_mm", 0, -1000000, 1000000);
		s.y = amount(v, "y_mm", 0, -1000000, 1000000);
		s.bearing = amount(v, "bearing_mdeg", 0, 0, 359999);
		s.arc = amount(v, "arc_mdeg", 360000, 1, 360000);
		if (s.kind == OperatingSystem::Weapon)
		{
			const auto &capability = directed.at(fit.id);
			const auto mode = std::find_if(capability.modes.begin(), capability.modes.end(),
			                               [](const auto &m)
			                               {
				                               return m.operation != DirectedOperationKind::Scan;
			                               });
			if (mode == capability.modes.end())
				fail("weapon requires at least one launch or beam mode");
			auto &weapon = s.get<WeaponDefinition>();
			weapon.family = mode->operation == DirectedOperationKind::Beam ? "beam" : "projectile";
			const auto magazine = v.value("magazine", std::string{});
			weapon.magazines = v.value("magazines", std::vector<std::string>{});
			if (weapon.magazines.empty() && !magazine.empty())
				weapon.magazines.push_back(magazine);
			weapon.turret = v.value("turret", std::string{});
			weapon.muzzle_marker = v.value("muzzle_marker", std::string{});
			weapon.ammunition = mode->ammunition;
			weapon.ammo_use = mode->ammo_use;
			weapon.range = mode->range;
			weapon.beam_damage = mode->beam_damage;
			weapon.standby_energy = mode->standby_energy;
			for (const auto &policy : capability.targeting)
				weapon.point_defence |= policy.kind == DirectedTargetingKind::Intercept;

			weapon.intercept_classes = v.value("intercept_classes", std::vector<std::string>{"missile"});
			for (const auto &tag : weapon.intercept_classes)
				if (tag != "missile")
					fail("unsupported intercept class");
			for (const auto &m : capability.modes)
				for (const auto &target : m.targeting)
					for (const auto &policy : capability.targeting)
						if (policy.id == target && policy.kind == DirectedTargetingKind::Intercept && !m.standby_energy)
							fail("missile interception mode requires standby energy");
		}
		if (s.kind == OperatingSystem::Shield)
		{
			if (v.contains("shield_effect"))
			{
				s.shield_effect = v.at("shield_effect").get<std::string>();
				s.shield_definition = compile_shield_effect(catalog, s.shield_effect, rules);
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
			for (const auto *legacy :
			     {"capacity", "initial", "reload", "ammunition_type", "fire_ticks", "load_ticks", "projectile"})
				if (v.contains(legacy))
					fail(std::string("magazine requires shared stock/resource profile; retired field ") + legacy);
			auto &magazine = s.get<MagazineDefinition>();
			magazine.stock = compile_stock_store(catalog, v.at("stock"), s.id);
			if (!magazine.stock.single_resource)
				fail("magazine stock must be single-resource");
			const auto resources = compile_resource_catalog(catalog);
			for (const auto &[id, resource] : resources)
				if (stock_filter_accepts(magazine.stock.filter, resource))
				{
					if (!resource.tags.contains("ammo"))
						fail("magazine filter accepts non-ammunition resource " + id);
					magazine.ammunition_profiles[id] = compile_ammunition_resource(catalog, id);
				}
			if (!magazine.stock.selection.empty())
			{
				magazine.ammunition = magazine.ammunition_profiles.at(magazine.stock.selection);
				magazine.reserve.capacity =
				    magazine.stock.capacity /
				    resources.at(magazine.stock.selection).sizes.at(magazine.stock.capacity_tag);
				magazine.reserve.initial = magazine.stock.initial.contains(magazine.stock.selection)
				                               ? magazine.stock.initial.at(magazine.stock.selection)
				                               : 0;
			}
			else
				magazine.reserve.capacity = magazine.stock.capacity;
			if (v.contains("processor"))
			{
				auto processor = v.at("processor");
				processor["equipment"] = s.id;
				if (!processor.contains("outputs"))
				{
					processor["outputs"] = Json::object();
					for (const auto &[resource, profile] : magazine.ammunition_profiles)
						processor["outputs"][resource] = "system:" + s.id;
				}
				magazine.processor = compile_processor(catalog, processor, s.id);
				if (magazine.processor->energy_per_second > 0 && !v.contains("power_store"))
					fail("funded magazine processor requires power_store");
				for (const auto &[resource, destination] : magazine.processor->outputs)
					if (destination != "system:" + s.id && destination != s.id)
						fail("magazine processor outputs must bind its own stock");
				for (const auto &recipe : magazine.processor->recipes)
				{
					if (recipe.outputs.size() != 1)
						fail("magazine recipe must output exactly one resource");
					for (const auto &[resource, quantity] : recipe.outputs)
						if (!magazine.ammunition_profiles.contains(resource) ||
						    (!magazine.processor->continuous &&
						     quantity > magazine.stock.capacity /
						                    resources.at(resource).sizes.at(magazine.stock.capacity_tag)))
							fail("magazine processor output is incompatible or larger than capacity");
				}
			}
		}
		if (s.kind == OperatingSystem::Team)
		{
			s.get<TeamDefinition>().repair_rate = amount(v, "hp_per_second", 10, 1, 1000000);
			s.get<TeamDefinition>().stack_penalty = amount(v, "stack_penalty_permille", 500, 1, 10000);
			auto &team = s.get<TeamDefinition>();
			team.local_repair_priority = v.value("local_repair_priority", false);
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
									if (a == "apply_effect_group")
									{
										if (task.arguments.size() != 2 ||
										    task.arguments[0].source != state_tree::Source::Literal ||
										    task.arguments[1].source != state_tree::Source::Literal)
											fail("team effect actions require literal group and target role");
										const auto &role = task.arguments[1].literal.text;
										if (role != "self" && role != "target")
											fail("team effect target must be self or target");
										if (!team_effect_properties)
											team_effect_properties = compile_property_catalog(catalog);
										(void)compile_effect_group(catalog, task.arguments[0].literal.text,
										                           team_effect_properties);
									}
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
				for (const auto &mode : directed.at(s.id).modes)
					if (mode.operation == DirectedOperationKind::Launch)
					{
						if (mode.ammo_use > d.systems[d.system_ids.at(mag)].reserve().capacity)
							fail(s.id + ": mode ammunition use exceeds magazine capacity");
						if (mode.ammunition.projectile.visual.empty())
							for (const auto &[resource, profile] :
							     d.systems[d.system_ids.at(mag)].get<MagazineDefinition>().ammunition_profiles)
								if (profile.projectile.visual.empty())
									fail(s.id + ": missing ammunition projectile for " + resource);
					}
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
