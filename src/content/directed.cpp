module;
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.content;
namespace phoenix
{
namespace
{
void directed_require(bool ok, const std::string &path, const std::string &reason)
{
	if (!ok)
		throw std::runtime_error(path + ": " + reason);
}
void directed_fields(const Json &value, std::set<std::string> fields, const std::string &path)
{
	directed_require(value.is_object(), path, "expected object");
	fields.insert({"id", "kind", "extends", "name", "label", "override"});
	for (const auto &[key, unused] : value.items())
		directed_require(fields.contains(key), path, "unsupported field " + key);
}
std::int64_t directed_amount(const Json &j, const char *key, std::int64_t fallback, std::int64_t minimum,
                             std::int64_t maximum, const std::string &path)
{
	const auto value = j.value(key, Json(fallback));
	directed_require(value.is_number_integer() && value >= minimum && value <= maximum, path,
	                 std::string("invalid bounded integer ") + key);
	return value.get<std::int64_t>();
}
std::vector<std::string> directed_names(const Json &j, const std::string &path, bool required = false)
{
	directed_require(j.is_array() && j.size() <= 32 && (!required || !j.empty()), path,
	                 "expected bounded reference array");
	std::vector<std::string> result;
	std::set<std::string> seen;
	for (const auto &value : j)
	{
		directed_require(value.is_string(), path, "expected reference string");
		const auto id = value.get<std::string>();
		directed_require(valid_identifier(id) && seen.insert(id).second, path, "invalid or duplicate reference");
		result.push_back(id);
	}
	return result;
}
const Json &directed_effect(const Catalog &catalog, const std::string &id, const std::string &kind)
{
	directed_require(catalog.effects.contains(id), id, "unknown catalogue reference");
	const auto &value = catalog.effects.at(id);
	directed_require(value.value("kind", std::string{}) == kind, id, "expected " + kind);
	return value;
}
} // namespace
bool property_compatible(std::string_view property, OperatingSystem kind, const DirectedDefinition &directed)
{
	if (property_compatible(property, kind))
		return true;
	for (const auto &mode : directed.modes)
		if ((property == "sensors" && mode.operation == DirectedOperationKind::Scan) ||
		    (property == "cycling" && mode.operation == DirectedOperationKind::Launch))
			return true;
	return false;
}
DirectedTargetingDefinition compile_targeting_policy(const Catalog &catalog, const std::string &id)
{
	const auto &j = directed_effect(catalog, id, "targeting_policy");
	directed_fields(j, {"policy"}, id);
	DirectedTargetingDefinition result;
	result.id = id;
	result.label = j.value("label", id);
	const auto policy = j.value("policy", std::string{});
	if (policy == "designated")
		result.kind = DirectedTargetingKind::Designated;
	else if (policy == "intercept")
		result.kind = DirectedTargetingKind::Intercept;
	else if (policy == "off")
		result.kind = DirectedTargetingKind::Off;
	else
		directed_require(false, id, "unsupported target policy");
	directed_require(!result.label.empty() && result.label.size() <= 256, id, "invalid label");
	return result;
}
std::vector<std::string> compile_payloads(const Catalog &catalog, const Json &refs, const std::string &path)
{
	// The catalogue validates payload bodies independently, including unused templates.
	// Fittings retain references, not copies of those immutable effect graphs.
	auto result = directed_names(refs, path);
	for (const auto &id : result)
		(void)directed_effect(catalog, id, "payload");
	return result;
}
PayloadDefinition compile_payload(const Catalog &catalog, const std::string &id)
{
	const auto &j = directed_effect(catalog, id, "payload");
	directed_fields(j, {"bindings"}, id);
	const auto &bindings = j.at("bindings");
	directed_require(bindings.is_array() && !bindings.empty() && bindings.size() <= 32, id, "expected 1..32 bindings");
	PayloadDefinition result{id, {}};
	std::set<std::string> seen;
	for (const auto &value : bindings)
	{
		directed_fields(value, {"outcome", "positive_damage", "effect"}, id);
		PayloadBindingDefinition binding;
		binding.id = value.value("id", std::string{});
		directed_require(valid_identifier(binding.id) && seen.insert(binding.id).second, id,
		                 "invalid binding identity");
		const auto outcome = value.value("outcome", std::string{});
		if (outcome == "shield")
			binding.outcome = PayloadOutcomeKind::Shield;
		else if (outcome == "compartment")
			binding.outcome = PayloadOutcomeKind::Compartment;
		else if (outcome == "system")
			binding.outcome = PayloadOutcomeKind::System;
		else if (outcome == "entity")
			binding.outcome = PayloadOutcomeKind::Entity;
		else
			directed_require(false, id, "unsupported hit outcome");
		binding.positive_damage = value.value("positive_damage", true);
		binding.reaction = compile_effect_group(catalog, value.at("effect").get<std::string>());
		for (const auto &operation : binding.reaction.operations)
		{
			if (operation.equipment == "$hit")
				directed_require(binding.outcome == PayloadOutcomeKind::System ||
				                     binding.outcome == PayloadOutcomeKind::Shield,
				                 id, "$hit equipment requires a system or shield binding");
			if (operation.kind == ShieldReactionKind::Apply &&
			    catalog.effects.at(operation.effect).value("kind", std::string{}) == "periodic")
				directed_require(binding.outcome == PayloadOutcomeKind::Compartment, id,
				                 "periodic damage conditions require a compartment outcome");
		}
		result.bindings.push_back(std::move(binding));
	}
	return result;
}
PeriodicEffectDefinition compile_periodic_effect(const Catalog &catalog, const std::string &id)
{
	const auto &j = directed_effect(catalog, id, "periodic");
	directed_fields(j, {"duration_ticks", "period_ticks", "effect"}, id);
	PeriodicEffectDefinition result;
	result.id = id;
	result.duration_ticks = directed_amount(j, "duration_ticks", 0, 1, 1000000, id);
	result.period_ticks = directed_amount(j, "period_ticks", 0, 1, result.duration_ticks, id);
	result.reaction = compile_effect_group(catalog, j.at("effect").get<std::string>());
	for (const auto &op : result.reaction.operations)
	{
		directed_require(op.equipment != "$hit", id, "periodic compartment has no equipment hit binding");
		if (op.kind == ShieldReactionKind::Apply)
			directed_require(catalog.effects.at(op.effect).value("kind", std::string{}) != "periodic", id,
			                 "periodic reactions cannot spawn periodic conditions");
	}
	return result;
}
DirectedModeDefinition compile_directed_mode(const Catalog &catalog, const std::string &id)
{
	const auto &j = directed_effect(catalog, id, "directed_mode");
	directed_fields(j,
	                {"operation", "targeting", "range_mm", "energy_per_second", "discrete_energy",
	                 "auto_energy_per_second", "ammo_use", "fire_ticks", "load_ticks", "projectile",
	                 "damage_per_second", "penetration", "damage_per_m", "shield_bypass_permille", "capabilities",
	                 "payloads", "resource_payloads", "firing_audio"},
	                id);
	DirectedModeDefinition result;
	result.id = id;
	result.label = j.value("label", id);
	directed_require(!result.label.empty() && result.label.size() <= 256, id, "invalid label");
	const auto operation = j.value("operation", std::string{});
	if (operation == "launch")
		result.operation = DirectedOperationKind::Launch;
	else if (operation == "beam")
		result.operation = DirectedOperationKind::Beam;
	else if (operation == "scan")
		result.operation = DirectedOperationKind::Scan;
	else
		directed_require(false, id, "unsupported directed operation");
	if (j.contains("firing_audio"))
	{
		directed_require(result.operation != DirectedOperationKind::Scan, id, "scan has no firing audio");
		result.firing_audio = compile_audio_clip(catalog, j.at("firing_audio"), id + ".firing_audio",
		                                         result.operation == DirectedOperationKind::Beam);
	}
	result.targeting = directed_names(j.value("targeting", Json::array()), id, true);
	for (const auto &target : result.targeting)
	{
		const auto policy = compile_targeting_policy(catalog, target);
		directed_require(result.operation == DirectedOperationKind::Launch ||
		                     policy.kind != DirectedTargetingKind::Intercept,
		                 id, "missile interception requires a launch mode");
	}
	result.range = directed_amount(j, "range_mm", 1000000, 1, 1000000000, id);
	result.energy_per_second = directed_amount(j, "energy_per_second", 0, 0, 1000000000, id);
	result.discrete_energy = directed_amount(j, "discrete_energy", 0, 0, 1000000000, id);
	result.standby_energy = directed_amount(j, "auto_energy_per_second", 0, 0, 1000000000, id);
	result.ammo_use = directed_amount(j, "ammo_use", 1, 1, 1000000, id);
	result.ammunition.interval = directed_amount(j, "fire_ticks", 20, 1, 1000000, id);
	result.ammunition.load_ticks = directed_amount(j, "load_ticks", 0, 0, 1000000, id);
	result.beam_damage = directed_amount(j, "damage_per_second", 1, 0, 1000000, id);
	result.resource_payloads = j.value("resource_payloads", true);
	result.payloads = compile_payloads(catalog, j.value("payloads", Json::array()), id + ".payloads");
	if (j.contains("projectile"))
		result.ammunition.projectile = compile_projectile_profile(catalog, j.at("projectile"), id);
	else if (result.operation == DirectedOperationKind::Beam)
	{
		result.ammunition.projectile.speed = 10000000;
		result.ammunition.projectile.penetration = directed_amount(j, "penetration", 1000, 1, 1000000, id);
		result.ammunition.projectile.deposition = directed_amount(j, "damage_per_m", 1000000, 1, 1000000, id);
		result.ammunition.projectile.shield_bypass = directed_amount(j, "shield_bypass_permille", 0, 0, 1000, id);
	}
	if (result.operation == DirectedOperationKind::Scan)
	{
		directed_require(!j.contains("projectile") && result.payloads.empty() && !result.discrete_energy, id,
		                 "scan uses funded native acquisition, not projectile/damage payloads");
		const auto caps = j.value("capabilities", Json{{"basic_identity", 0}, {"own_arcs", 0}});
		const std::set<std::string> allowed{"basic_identity",        "own_arcs", "contact_arcs", "shield_sectors",
		                                    "compartment_condition", "findings"};
		directed_require(caps.is_object() && caps.size() <= allowed.size(), id, "invalid scan capabilities");
		for (const auto &[key, value] : caps.items())
		{
			directed_require(allowed.contains(key) && value.is_number_integer() && value >= 0 && value <= 1728000, id,
			                 "invalid capability or acquisition duration");
			result.capabilities[key] = value.get<std::int64_t>();
		}
	}
	else
		directed_require(!j.contains("capabilities"), id, "only scan modes own acquisition capabilities");
	if (result.operation == DirectedOperationKind::Beam)
		directed_require(result.energy_per_second > 0, id, "beam requires positive operating energy");
	return result;
}
std::map<std::string, DirectedDefinition> compile_directed(const Catalog &catalog, const std::string &entity)
{
	std::map<std::string, DirectedDefinition> out;
	// Client presentation DATs deliberately omit authoritative modes and payloads.
	if (catalog.presentation)
		return out;
	const auto &hull = catalog.entities.at(entity);
	std::set<std::string> mounts;
	const auto systems = hull.value("systems", Json::object());
	for (const auto &[system, spec] : systems.items())
	{
		const auto kind = spec.value("kind", system);
		if (!spec.contains("directed"))
		{
			directed_require(kind != "weapon", system, "weapon requires directed capability");
			continue;
		}
		directed_require(kind == "weapon" || kind == "radar", system,
		                 "directed fitting requires weapon or radar native mechanism");
		if (kind == "weapon")
			for (const auto *field : {"family", "fire_ticks", "ammo_use", "load_ticks", "range_mm", "damage_per_second",
			                          "auto_energy_per_second", "energy_per_second", "discrete_energy", "projectile",
			                          "penetration", "damage_per_m", "shield_bypass_permille", "point_defence"})
				directed_require(!spec.contains(field), system, std::string("retired inline directed field ") + field);
		directed_require(!spec.contains("capabilities"), system,
		                 "acquisition capabilities belong to the directed mode");
		const auto &j = spec.at("directed");
		directed_fields(j, {"modes", "targeting", "initial_mode", "initial_targeting"}, system);
		DirectedDefinition definition;
		for (const auto &id : directed_names(j.value("targeting", Json::array()), system, true))
			definition.targeting.push_back(compile_targeting_policy(catalog, id));
		for (const auto &id : directed_names(j.value("modes", Json::array()), system, true))
		{
			auto mode = compile_directed_mode(catalog, id);
			bool supported = false;
			for (const auto &target : mode.targeting)
				for (const auto &policy : definition.targeting)
					if (target == policy.id)
						supported = true;
			directed_require(supported, system, "mode has no supported targeting policy");
			if (mode.energy_per_second || mode.discrete_energy || mode.standby_energy)
				directed_require(spec.contains("power_store"), system, "funded directed mode requires power_store");
			if (mode.operation != DirectedOperationKind::Scan)
				directed_require(hull.contains("combat"), system,
				                 "launch and beam modes require native combat structure");
			if (mode.operation == DirectedOperationKind::Launch)
				directed_require(spec.contains("magazine") ||
				                     (spec.contains("magazines") && !spec.at("magazines").empty()),
				                 system, "launch mode requires magazine binding");
			definition.modes.push_back(std::move(mode));
		}
		for (const auto &policy : definition.targeting)
		{
			bool supported = false;
			for (const auto &mode : definition.modes)
				supported |= std::find(mode.targeting.begin(), mode.targeting.end(), policy.id) != mode.targeting.end();
			directed_require(supported, system, "targeting policy has no supported operating mode");
		}
		const auto initial_mode = j.value("initial_mode", definition.modes.front().id);
		const auto initial_targeting = j.value("initial_targeting", definition.targeting.front().id);
		const auto mode = std::find_if(definition.modes.begin(), definition.modes.end(),
		                               [&](const auto &m)
		                               {
			                               return m.id == initial_mode;
		                               });
		const auto target = std::find_if(definition.targeting.begin(), definition.targeting.end(),
		                                 [&](const auto &p)
		                                 {
			                                 return p.id == initial_targeting;
		                                 });
		directed_require(mode != definition.modes.end() && target != definition.targeting.end(), system,
		                 "unknown initial selection");
		directed_require(std::find(mode->targeting.begin(), mode->targeting.end(), target->id) != mode->targeting.end(),
		                 system, "incompatible initial selections");
		definition.initial_mode = static_cast<std::size_t>(mode - definition.modes.begin());
		definition.initial_targeting = static_cast<std::size_t>(target - definition.targeting.begin());
		// Separate mode/policy commands must be able to reach every valid selection.
		std::set<std::string> reached_modes{initial_mode}, reached_targets{initial_targeting};
		bool expanded = true;
		while (expanded)
		{
			expanded = false;
			for (const auto &candidate : definition.modes)
				for (const auto &policy : definition.targeting)
					if (std::find(candidate.targeting.begin(), candidate.targeting.end(), policy.id) !=
					        candidate.targeting.end() &&
					    (reached_modes.contains(candidate.id) || reached_targets.contains(policy.id)))
					{
						expanded |= reached_modes.insert(candidate.id).second;
						expanded |= reached_targets.insert(policy.id).second;
					}
		}
		directed_require(
		    reached_modes.size() == definition.modes.size() && reached_targets.size() == definition.targeting.size(),
		    system,
		    "mode/targeting compatibility graph is disconnected; separate selection commands cannot reach all modes");
		const auto turret = spec.value("turret", std::string{});
		if (!turret.empty())
		{
			directed_require(hull.at("systems").contains(turret) &&
			                     hull.at("systems").at(turret).value("kind", turret) == "turret",
			                 system, "unknown turret binding");
			directed_require(mounts.insert(turret).second, system,
			                 "physical turret requires one directed targeting owner");
		}
		out.emplace(system, std::move(definition));
	}
	return out;
}
DirectedCatalog compile_directed_catalog(const Catalog &catalog)
{
	DirectedCatalog result;
	for (const auto &[id, entity] : catalog.entities)
		result.emplace(id, compile_directed(catalog, id));
	return result;
}
} // namespace phoenix
