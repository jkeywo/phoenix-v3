module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.gameplay.combat.continuation;
import phoenix.state_tree;
namespace phoenix
{
namespace
{
constexpr std::int64_t rate = resource_tick_hz;
}

const TeamOrderDefinition *find_combat_team_order(const CombatMechanism &m, const std::string &id)
{
	for (const auto &order : m.definition.get<TeamDefinition>().orders)
		if (order.id == id)
			return &order;
	return nullptr;
}
Json save_combat_structure(const CombatStructure &s)
{
	Json j = {{"random", s.random}, {"faults", s.faults}, {"compartments", Json::array()}, {"systems", Json::object()}};
	j["closed_routes"] = s.closed_routes;
	j["access_revision"] = s.access_revision;
	for (const auto &c : s.compartments)
		j["compartments"].push_back({{"hp", c.hp}, {"work", c.work}, {"disabled", c.disabled}});
	return j;
}
Json save_combat_mechanism(const CombatMechanism &m)
{
	auto result = std::visit(
	    [](const auto &state)
	    {
		    if constexpr (std::is_same_v<std::decay_t<decltype(state)>, ShieldBindingState>)
			    return Json::object();
		    else
			    return Json(state);
	    },
	    m.state);
	if (m.definition.kind == OperatingSystem::Team)
		if (const auto *order = find_combat_team_order(m, m.get<TeamState>().order))
			result["order_continuation"] = state_tree::save(order->program, m.get<TeamState>().continuation);
	return result;
}
void restore_combat_structure(CombatStructure &s, const Json &j, bool destroyed_owner)
{
	s.closed_routes = j.at("closed_routes").get<std::set<std::string>>();
	s.access_revision = j.at("access_revision");
	if (!s.access_revision || s.access_revision >= 9007199254740991ULL ||
	    s.closed_routes.size() > s.definition.connections.size())
		throw std::runtime_error("Invalid compartment access continuation");
	for (const auto &key : s.closed_routes)
	{
		bool found = false;
		for (const auto &edge : s.definition.connections)
			found |= key == (std::to_string(std::min(edge.from, edge.to)) + ":" +
			                 std::to_string(std::max(edge.from, edge.to)));
		if (!found)
			throw std::runtime_error("Unknown closed compartment route");
	}
	s.random = j.at("random");
	s.faults = j.value("faults", Json(s.faults)).get<std::map<std::string, std::vector<FaultState>>>();
	for (const auto &[id, defs] : s.definition.faults)
		if (!s.faults.contains(id) || s.faults.at(id).size() != defs.size())
			throw std::runtime_error("Fault roster mismatch");
	if (j.at("compartments").size() != s.compartments.size())
		throw std::runtime_error("Combat compartment roster mismatch");
	for (std::size_t i = 0; i < s.compartments.size(); ++i)
	{
		const auto &v = j.at("compartments")[i];
		auto &c = s.compartments[i];
		c.hp = checked_integer(v.at("hp"), 0, s.definition.compartments[i].maximum, "compartment.hp");
		c.work = checked_integer(v.at("work"), 0, rate * 1000 - 1, "compartment.work");
		c.disabled = v.at("disabled");
		if ((c.hp == 0 && !c.disabled) || (c.hp >= s.definition.compartments[i].recovery && c.disabled))
			throw std::runtime_error("Invalid structural disablement");
	}
	const auto destroyed = std::all_of(s.compartments.begin(), s.compartments.end(),
	                                   [](const auto &c)
	                                   {
		                                   return c.hp == 0;
	                                   });
	if (destroyed != destroyed_owner)
		throw std::runtime_error("Inconsistent ship destruction continuation");
	if (j.at("systems").size() != s.definition.systems.size())
		throw std::runtime_error("Combat equipment roster mismatch");
}
void restore_combat_mechanism(CombatMechanism &mechanism, const CombatDefinition &definition, const Json &v)
{
	const auto n = static_cast<std::int64_t>(definition.compartments.size());
	std::visit(
	    [&](auto &state)
	    {
		    using State = std::decay_t<decltype(state)>;
		    if constexpr (std::is_same_v<State, ShieldBindingState>)
		    {
			    if (!v.is_object() || !v.empty())
				    throw std::runtime_error("Invalid shield binding state");
		    }
		    else
		    {
			    state = v.get<State>();
			    Json expected = Json(state), actual = v;
			    if constexpr (std::is_same_v<State, TeamState>)
			    {
				    actual.erase("order_continuation");
				    for (const auto *key : {"revision", "order", "activity", "patient", "work_done", "condition",
				                            "treatment", "detained", "compartment_assignment", "medical_compartment"})
					    if (!actual.contains(key))
						    actual[key] = expected.at(key);
			    }
			    if (expected != actual)
				    throw std::runtime_error("Unexpected combat state fields");
		    }
	    },
	    mechanism.state);
	const auto bounded = [&](const char *key, std::int64_t low, std::int64_t high)
	{
		checked_integer(v.at(key), low, high, mechanism.definition.id + "." + key);
	};
	switch (mechanism.definition.kind)
	{
	case OperatingSystem::Weapon:
	{
		const auto &w = mechanism.get<WeaponState>();
		bounded("cooldown", 0, 1000000000);
		bounded("shots", 0, 1000000000000LL);
		bounded(
		    "selected_magazine", 0,
		    std::max<std::int64_t>(0, mechanism.definition.get<WeaponDefinition>().magazines.size() -
		                                  (mechanism.definition.get<WeaponDefinition>().magazines.empty() ? 0 : 1)));
		bounded("loaded_magazine", -1,
		        static_cast<std::int64_t>(mechanism.definition.get<WeaponDefinition>().magazines.size()) - 1);
		bounded("beam_remainder", 0, rate * 1000 - 1);
		bounded("beam_tick", -1, 1000000000000LL);
		bounded("bypass_remainder", 0, 999);
		if (w.loading < 0 || w.unloading < 0)
			throw std::runtime_error("Invalid ammunition progress");
		break;
	}
	case OperatingSystem::Turret:
		bounded("heading", 0, 359999);
		bounded("turn_remainder", 0, rate - 1);
		break;
	case OperatingSystem::Magazine:
	{
		const auto &a = mechanism.get<MagazineState>();
		if (a.stock != "system:" + mechanism.definition.id)
			throw std::runtime_error("Invalid magazine stock binding");
		break;
	}
	case OperatingSystem::Team:
	{
		auto &r = mechanism.get<TeamState>();
		if (!r.revision || r.revision > 9007199254740991ULL || r.patient.size() > 72)
			throw std::runtime_error("Invalid team revision/patient");
		if (!r.order.empty())
		{
			const auto *order = find_combat_team_order(mechanism, r.order);
			if (!order || !v.contains("order_continuation") || r.repair_auto || r.destination < 0)
				throw std::runtime_error("Invalid saved team order");
			if (!r.activity.empty() &&
			    std::none_of(order->program.registry.actions.begin(), order->program.registry.actions.end(),
			                 [&](const auto &a)
			                 {
				                 return a.name == r.activity;
			                 }))
				throw std::runtime_error("Invalid team activity");
			r.continuation = state_tree::load(order->program, v.at("order_continuation"));
		}
		else if (!r.activity.empty() || !r.patient.empty() || v.contains("order_continuation"))
			throw std::runtime_error("Orphaned team continuation");
		bounded("location", 0, n - 1);
		bounded("destination", -1, n - 1);
		bounded("edge_to", -1, n - 1);
		bounded("travel", 0, 1000000);
		bounded("edge_total", 0, 1000000);
		bounded("repair_system", -2, static_cast<std::int64_t>(definition.equipment_compartments.size()) - 1);
		if ((r.travel > 0) != (r.edge_to >= 0) || r.travel > r.edge_total)
			throw std::runtime_error("Invalid team traversal");
		break;
	}
	case OperatingSystem::Shield:
		break;
	default:
		throw std::runtime_error("Unexpected combat mechanism");
	}
}
Json save_shield_continuation(const ShieldComponent &owner)
{
	Json result = {{"next_effect", owner.next_effect}, {"contributions", Json::array()}};
	for (const auto &c : owner.contributions)
		result["contributions"].push_back({{"effect_id", c.effect_id},
		                                   {"name", c.definition.name},
		                                   {"equipment", c.equipment},
		                                   {"definition", c.definition_id},
		                                   {"source", c.source},
		                                   {"expiry", c.expiry},
		                                   {"state", c.state},
		                                   {"operating", c.operating},
		                                   {"protection", c.protection},
		                                   {"quoted_rate", c.quoted_rate},
		                                   {"quoted_sustain", c.quoted_sustain},
		                                   {"quoted_discharge", c.quoted_discharge}});
	return result;
}
ShieldComponent restore_shield_continuation(const Json &value, const CombatMechanismDefinitions &equipment_definitions,
                                            const std::map<std::string, ShieldEffectDefinition> &templates)
{
	ShieldComponent restored;
	restored.next_effect = checked_integer(value.at("next_effect"), 1, 9007199254740991LL, "shield.next_effect");
	if (!value.at("contributions").is_array() || value.at("contributions").size() > 8192)
		throw std::runtime_error("Invalid shield contribution roster");
	std::set<std::pair<std::uint64_t, std::string>> identities;
	for (const auto &v : value.at("contributions"))
	{
		ShieldContribution c;
		c.effect_id = checked_integer(v.at("effect_id"), 1, static_cast<std::int64_t>(restored.next_effect) - 1,
		                              "shield.effect_id");
		c.definition.name = v.at("name").get<std::string>();
		c.equipment = v.at("equipment").get<std::string>();
		c.definition_id = v.at("definition").get<std::string>();
		if (!identities.emplace(c.effect_id, c.definition.name).second)
			throw std::runtime_error("Duplicate shield contribution");
		const ShieldEffectDefinition *base = nullptr;
		if (!c.equipment.empty())
		{
			const auto fitted = equipment_definitions.find(c.equipment);
			if (fitted == equipment_definitions.end())
				throw std::runtime_error("Unknown shield equipment binding");
			const auto &equipment = fitted->second;
			if (equipment.kind != OperatingSystem::Shield || equipment.shield_effect != c.definition_id)
				throw std::runtime_error("Invalid shield definition binding");
			base = &equipment.shield_definition;
		}
		else
		{
			const auto at = templates.find(c.definition_id);
			if (at == templates.end())
				throw std::runtime_error("Unknown saved shield effect");
			base = &at->second;
		}
		c.definition = shield_contribution(*base, c.definition.name);
		c.source = v.at("source").get<std::string>();
		if (!c.equipment.empty() && c.source != c.equipment)
			throw std::runtime_error("Invalid shield equipment source");
		if (c.source.empty() || c.source.size() > 512)
			throw std::runtime_error("Invalid shield source identity");
		c.expiry = checked_integer(v.at("expiry"), -1, 9007199254740991LL, "shield.expiry");
		c.state = v.at("state").get<ShieldState>();
		if (Json(c.state) != v.at("state"))
			throw std::runtime_error("Unexpected shield state fields");
		const auto &d = c.definition.shield;
		checked_integer(v.at("state").at("strength"), 0, d.reserve.capacity, "shield.strength");
		checked_integer(v.at("state").at("warmup"), 0, d.warmup_ticks, "shield.warmup");
		checked_integer(v.at("state").at("recharge_remainder"), 0, rate * 1000 - 1, "shield.recharge_remainder");
		checked_integer(v.at("state").at("discharge_remainder"), 0, rate * 1000, "shield.discharge_remainder");
		checked_integer(v.at("state").at("discharge_credit"), 0, d.recharge_energy * d.discharge_efficiency - 1,
		                "shield.discharge_credit");
		c.operating = v.at("operating").get<bool>();
		c.protection = checked_integer(v.at("protection"), 0, 1000, "shield.protection");
		c.quoted_rate = checked_integer(v.at("quoted_rate"), 0, 1000000000000LL, "shield.quoted_rate");
		c.quoted_sustain = checked_integer(v.at("quoted_sustain"), 0, 1000000000000LL, "shield.quoted_sustain");
		c.quoted_discharge = checked_integer(v.at("quoted_discharge"), 0, 1000000000000LL, "shield.quoted_discharge");
		restored.contributions.push_back(std::move(c));
	}
	std::map<std::uint64_t, std::vector<const ShieldContribution *>> groups;
	std::map<std::string, std::uint64_t> equipment_grants;
	for (const auto &c : restored.contributions)
	{
		groups[c.effect_id].push_back(&c);
		if (!c.equipment.empty())
		{
			const auto [entry, inserted] = equipment_grants.emplace(c.equipment, c.effect_id);
			if (!inserted && entry->second != c.effect_id)
				throw std::runtime_error("Duplicate equipment shield grant");
		}
	}
	for (const auto &[id, group] : groups)
	{
		const auto &first = *group.front();
		const auto &base = first.equipment.empty() ? templates.at(first.definition_id)
		                                           : equipment_definitions.at(first.equipment).shield_definition;
		if (group.size() != base.contributions.size())
			throw std::runtime_error("Incomplete shield effect group");
		for (const auto *c : group)
			if (c->equipment != first.equipment || c->definition_id != first.definition_id ||
			    c->source != first.source || c->expiry != first.expiry)
				throw std::runtime_error("Inconsistent shield effect group");
	}
	for (const auto &[id, m] : equipment_definitions)
		if (m.kind == OperatingSystem::Shield && !equipment_grants.contains(id))
			throw std::runtime_error("Missing equipment shield grant");
	return restored;
}

Json save_projectile_continuation(const Projectile &p, const Destructible &native)
{
	return {{"id", p.id},
	        {"source", p.source},
	        {"weapon", p.weapon},
	        {"payloads", p.payloads},
	        {"rule_captures", p.rule_captures},
	        {"destruction_rule_captures", p.destruction_rule_captures},
	        {"position", p.position},
	        {"velocity", p.velocity},
	        {"remainder", p.remainder},
	        {"heading", p.heading},
	        {"life", p.life},
	        {"penetration", p.penetration},
	        {"damage", p.damage},
	        {"deposit_remainder", p.deposit_remainder},
	        {"resistance_remainder", p.resistance_remainder},
	        {"decks", p.decks},
	        {"definition", p.definition},
	        {"target", p.target},
	        {"altitude", p.altitude},
	        {"launch_altitude", p.launch_altitude},
	        {"bearing_launch", p.bearing_launch},
	        {"seeker_acquired", p.seeker_acquired},
	        {"arc_start_age", p.arc_start_age},
	        {"seeker_altitude", p.seeker_altitude},
	        {"vz", p.vz},
	        {"z_remainder", p.z_remainder},
	        {"durability", native.durability},
	        {"rule_notified", native.rule_notified},
	        {"age", p.age},
	        {"initial_distance", p.initial_distance},
	        {"base_heading", p.base_heading},
	        {"turn_remainder", p.turn_remainder},
	        {"random", p.random},
	        {"shield_hits", p.shield_hits},
	        {"missile_hits", p.missile_hits}};
}
ProjectileContinuationRoster restore_projectile_continuations(const Json &saved, std::int64_t tick,
                                                              const OrdnanceRestoreCatalogue &catalogue)
{
	const std::uint64_t next_projectile =
	    checked_integer(saved.at("next_projectile"), 1, 9007199254740991LL, "next_projectile");
	ProjectileContinuationRoster result;
	result.next_projectile = next_projectile;
	const auto list = saved.value("projectiles", Json::array());
	if (!list.is_array() || list.size() > 8192)
		throw std::runtime_error("Invalid projectile list");
	std::set<std::uint64_t> seen;
	for (const auto &v : list)
	{
		Projectile p;
		p.id = v.at("id");
		p.source = v.at("source");
		p.weapon = v.value("weapon", std::string{});
		if (p.weapon.size() > 256 || !v.at("payloads").is_array() || v.at("payloads").size() > 256)
			throw std::runtime_error("Invalid captured projectile payloads");
		p.payloads = v.at("payloads").get<std::vector<std::string>>();
		const auto validate_payloads = [&](const std::vector<std::string> &payloads)
		{
			std::set<std::string> captured;
			if (payloads.size() > 256)
				throw std::runtime_error("Too many projectile payloads");
			for (const auto &payload : payloads)
				if (!captured.insert(payload).second || !catalogue.payloads.contains(payload))
					throw std::runtime_error("Invalid captured payload identity");
		};
		validate_payloads(p.payloads);
		for (const auto *field : {"rule_captures", "destruction_rule_captures"})
			if (!v.at(field).is_array() || v.at(field).size() > rule_expression_node_limit)
				throw std::runtime_error("Invalid projectile capture roster");
		p.rule_captures = v.at("rule_captures").get<std::vector<RuleCapturedFact>>();
		p.position = v.at("position").get<fixed::Vec>();
		p.velocity = v.at("velocity").get<fixed::Vec>();
		p.remainder = v.at("remainder").get<fixed::Vec>();
		p.heading = v.at("heading");
		p.life = v.at("life");
		p.penetration = v.at("penetration");
		p.damage = v.at("damage");
		p.deposit_remainder = v.at("deposit_remainder");
		p.resistance_remainder = v.at("resistance_remainder");
		p.decks = v.at("decks").get<std::map<EntityId, std::string>>();
		p.definition = v.at("definition").get<ProjectileDefinition>();
		validate_payloads(p.definition.payloads);
		validate_payloads(p.definition.destruction.payloads);
		p.destruction_rule_captures = v.at("destruction_rule_captures").get<std::vector<RuleCapturedFact>>();
		p.target = v.at("target").get<decltype(p.target)>();
		p.altitude = v.at("altitude").get<decltype(p.altitude)>();
		p.launch_altitude =
		    checked_integer(v.value("launch_altitude", Json(0)), -1000000, 1000000, "projectile.launch_altitude");
		p.bearing_launch = v.value("bearing_launch", false);
		p.seeker_acquired = v.value("seeker_acquired", false);
		p.arc_start_age = checked_integer(v.value("arc_start_age", Json(0)), 0, v.at("age").get<std::int64_t>(),
		                                  "projectile.arc_start_age");
		p.seeker_altitude = checked_integer(v.value("seeker_altitude", Json(p.launch_altitude)), -1000000, 1000000,
		                                    "projectile.seeker_altitude");
		p.vz = v.at("vz").get<decltype(p.vz)>();
		p.z_remainder = v.at("z_remainder").get<decltype(p.z_remainder)>();
		const auto durability =
		    checked_integer(v.at("durability"), 0, p.definition.durability, "projectile.durability");
		if (!v.at("rule_notified").is_boolean())
			throw std::runtime_error("Invalid projectile destruction notification");
		const auto notified = v.at("rule_notified").get<bool>();
		if (notified && durability > 0)
			throw std::runtime_error("Living projectile has a destruction notification");
		p.age = v.at("age").get<decltype(p.age)>();
		p.initial_distance = v.at("initial_distance").get<decltype(p.initial_distance)>();
		p.base_heading = v.at("base_heading").get<decltype(p.base_heading)>();
		p.turn_remainder = v.at("turn_remainder").get<decltype(p.turn_remainder)>();
		p.random = v.at("random").get<decltype(p.random)>();
		p.shield_hits = v.at("shield_hits").get<decltype(p.shield_hits)>();
		p.missile_hits = v.at("missile_hits").get<decltype(p.missile_hits)>();
		checked_integer(v.at("id"), 1, next_projectile - 1, "projectile.id");
		checked_integer(v.at("source"), 0, 9007199254740991LL, "projectile.source");
		const bool effect_only = p.definition.damage == 0;
		if (!seen.insert(p.id).second || p.id >= next_projectile || !catalogue.visuals.contains(p.definition.visual) ||
		    p.life < 1 || p.life > p.definition.lifetime || p.damage < (effect_only ? 0 : 1) ||
		    p.damage > p.definition.damage || p.penetration < (effect_only ? 0 : 1) ||
		    p.penetration > p.definition.penetration || std::abs(p.position.x) > fixed::unit ||
		    std::abs(p.position.y) > fixed::unit || std::abs(p.velocity.x) > fixed::unit ||
		    std::abs(p.velocity.y) > fixed::unit || std::abs(p.remainder.x) >= rate ||
		    std::abs(p.remainder.y) >= rate || p.deposit_remainder < 0 || p.deposit_remainder >= 1000 ||
		    p.resistance_remainder < 0 || p.resistance_remainder >= 1000)
			throw std::runtime_error("Invalid projectile continuation");
		for (const auto &[id, deck] : p.decks)
			if (!catalogue.decks.contains(id) || !catalogue.decks.at(id).contains(deck))
				throw std::runtime_error("Invalid projectile deck selection");
		Destructible native{p.definition.destruction, durability};
		native.rule_notified = notified;
		if (notified)
			native.destroyed_tick = tick;
		result.values.push_back({std::move(p), std::move(native)});
	}

	return result;
}
} // namespace phoenix
