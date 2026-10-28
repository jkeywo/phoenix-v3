module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>
#include <utility>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.gameplay.directed;
namespace phoenix
{
DirectedRuntime initial_directed_runtime(const DirectedDefinition &definition)
{
	DirectedRuntime state;
	state.operating_mode = definition.initial_mode;
	state.targeting_mode = definition.initial_targeting;
	return state;
}
bool directed_mode_compatible(const DirectedDefinition &d, std::size_t mode, std::size_t targeting)
{
	if (mode >= d.modes.size() || targeting >= d.targeting.size())
		return false;
	const auto &allowed = d.modes[mode].targeting;
	return allowed.empty() || std::find(allowed.begin(), allowed.end(), d.targeting[targeting].id) != allowed.end();
}
DirectedModeSelection select_directed_modes(DirectedRuntime &state, const DirectedDefinition &d,
                                            const std::string &suffix, std::int64_t value, bool execute)
{
	if (value < 0 || (suffix != "operating_mode" && suffix != "targeting_mode"))
		return {};
	const auto mode = suffix == "operating_mode" ? static_cast<std::size_t>(value) : state.operating_mode;
	const auto targeting = suffix == "targeting_mode" ? static_cast<std::size_t>(value) : state.targeting_mode;
	if (!directed_mode_compatible(d, mode, targeting))
		return {};
	const bool changed = state.operating_mode != mode || state.targeting_mode != targeting;
	if (execute)
	{
		state.operating_mode = mode;
		state.targeting_mode = targeting;
		if (changed)
		{
			state.ready = false;
			state.blocker = "Mode changed";
		}
	}
	return {true, changed};
}
bool directed_target_visible(const Observation &observation, EntityId target, bool retained)
{
	return retained ||
	       std::any_of(observation.contacts.begin(), observation.contacts.end(),
	                   [&](const auto &c)
	                   {
		                   return c.id == target;
	                   }) ||
	       std::any_of(observation.bearing_contacts.begin(), observation.bearing_contacts.end(),
	                   [&](const auto &c)
	                   {
		                   return c.id == target;
	                   });
}
std::optional<DirectedAim> select_directed_contact(const Observation &observation, EntityId target,
                                                   std::int64_t observer_altitude, bool allow_bearing)
{
	if (!target)
		return {};
	for (const auto &contact : observation.contacts)
		if (contact.id == target)
			return DirectedAim{contact, contact.altitude, contact.vz};
	if (allow_bearing)
		for (const auto &bearing : observation.bearing_contacts)
			if (bearing.id == target)
			{
				DirectedAim aim;
				aim.body.id = target;
				aim.bearing = bearing.bearing_mdeg;
				aim.altitude = observer_altitude;
				return aim;
			}
	return {};
}
bool apply_directed_weapon_mode(CombatMechanism &mechanism, const DirectedModeDefinition &mode, bool restoring)
{
	auto &weapon = mechanism.definition.get<WeaponDefinition>();
	weapon.range = mode.range;
	if (mode.operation != DirectedOperationKind::Scan)
	{
		weapon.ammunition = mode.ammunition;
		weapon.ammo_use = mode.ammo_use;
	}
	weapon.standby_energy = mode.standby_energy;
	weapon.beam_damage = mode.beam_damage;
	if (mode.operation != DirectedOperationKind::Scan)
		weapon.family = mode.operation == DirectedOperationKind::Beam ? "beam" : "projectile";
	auto &state = mechanism.get<WeaponState>();
	if (!restoring)
	{
		state.ready = false;
		state.beam_tick = -1;
	}
	return !restoring && mode.operation == DirectedOperationKind::Launch && state.loaded_magazine >= 0 &&
	       state.loaded_quantity != mode.ammo_use && !state.unloading;
}
DirectedDemand directed_resource_demand(const DirectedModeDefinition &mode, DirectedTargetingKind targeting,
                                        bool enabled, bool weapon_ready, bool broad_radar)
{
	const bool working = enabled && (mode.operation == DirectedOperationKind::Scan || weapon_ready);
	const auto standby = enabled && targeting == DirectedTargetingKind::Intercept ? mode.standby_energy : 0;
	const auto operation_rate = working ? mode.energy_per_second : 0;
	if (operation_rate > resource_limit - standby)
		throw std::runtime_error("Directed operating rate exceeds supported bound");
	return {broad_radar ? 1000 : 0, working && mode.operation == DirectedOperationKind::Launch,
	        operation_rate + standby};
}
bool begin_directed_operation(DirectedRuntime &state, DirectedOperationKind kind, bool enabled, bool qualified,
                              const DirectedPower &power)
{
	state.ready = false;
	if (!enabled)
	{
		state.blocker = "Directed operation inactive";
		return false;
	}
	if (!qualified)
	{
		state.blocker = "Target or operation not ready";
		return false;
	}
	if (power.present &&
	    (power.blocked ||
	     (kind == DirectedOperationKind::Beam ? power.supply_permille <= 0 : power.supply_permille < 1000) ||
	     (kind == DirectedOperationKind::Launch && power.discrete_required && !power.discrete_paid)))
	{
		state.blocker = "Insufficient operating power";
		return false;
	}
	state.ready = true;
	state.blocker.clear();
	return true;
}
bool finish_directed_operation(DirectedRuntime &state, bool completed)
{
	if (!completed)
	{
		state.ready = false;
		state.blocker = "Required operation unavailable";
		return false;
	}
	state.sequence = std::min<std::uint64_t>(9007199254740991ULL, state.sequence + 1);
	return true;
}
Json project_directed_runtime(const DirectedDefinition &d, const DirectedRuntime &state)
{
	Json result{{"operating_modes", Json::array()},
	            {"targeting_modes", Json::array()},
	            {"operating_mode", state.operating_mode},
	            {"targeting_mode", state.targeting_mode},
	            {"target", state.target},
	            {"ready", state.ready},
	            {"blocker", state.blocker},
	            {"sequence", state.sequence}};
	for (std::size_t i = 0; i < d.modes.size(); ++i)
		result["operating_modes"].push_back({{"id", d.modes[i].id},
		                                     {"label", d.modes[i].label},
		                                     {"value", i},
		                                     {"compatible", directed_mode_compatible(d, i, state.targeting_mode)}});
	for (std::size_t i = 0; i < d.targeting.size(); ++i)
		result["targeting_modes"].push_back({{"id", d.targeting[i].id},
		                                     {"label", d.targeting[i].label},
		                                     {"value", i},
		                                     {"compatible", directed_mode_compatible(d, state.operating_mode, i)}});
	return result;
}
Json save_directed_runtime(EntityId ship, const std::string &equipment, const DirectedRuntime &state)
{
	return {{"ship", ship},
	        {"system", equipment},
	        {"operating_mode", state.operating_mode},
	        {"targeting_mode", state.targeting_mode},
	        {"target", state.target},
	        {"sequence", state.sequence},
	        {"loading_mode", state.loading_mode},
	        {"loaded_mode", state.loaded_mode},
	        {"beam_funding_remainder", state.beam_funding_remainder},
	        {"ready", state.ready},
	        {"blocker", state.blocker}};
}
DirectedRuntime restore_directed_runtime(const Json &row, const DirectedDefinition &d, EntityId ship)
{
	DirectedRuntime state;
	state.operating_mode = checked_integer(row.at("operating_mode"), 0, 255, "directed.operating_mode");
	state.targeting_mode = checked_integer(row.at("targeting_mode"), 0, 255, "directed.targeting_mode");
	state.target = checked_integer(row.at("target"), 0, 7999999999999999LL, "directed.target");
	if (!directed_mode_compatible(d, state.operating_mode, state.targeting_mode) || state.target == ship)
		throw std::runtime_error("Invalid saved directed selection");
	state.sequence = checked_integer(row.at("sequence"), 0, 9007199254740991LL, "directed.sequence");
	state.loading_mode = row.at("loading_mode").get<std::string>();
	state.loaded_mode = row.at("loaded_mode").get<std::string>();
	state.beam_funding_remainder =
	    checked_integer(row.at("beam_funding_remainder"), 0, 999, "directed.beam_funding_remainder");
	for (const auto &mode : {state.loading_mode, state.loaded_mode})
		if (!mode.empty() && std::none_of(d.modes.begin(), d.modes.end(),
		                                  [&](const auto &m)
		                                  {
			                                  return m.id == mode && m.operation == DirectedOperationKind::Launch;
		                                  }))
			throw std::runtime_error("Invalid saved loading mode");
	state.ready = row.at("ready").get<bool>();
	state.blocker = row.at("blocker").get<std::string>();
	if (state.blocker.size() > 256)
		throw std::runtime_error("Invalid saved directed blocker");
	return state;
}
void validate_directed_ammunition_commitment(const DirectedRuntime &state, const WeaponState *native)
{
	if (native)
	{
		if ((native->loading > 0) != !state.loading_mode.empty() ||
		    (native->loaded_magazine >= 0) != !state.loaded_mode.empty())
			throw std::runtime_error("Saved weapon loading state disagrees with its committed mode");
	}
	else if (!state.loading_mode.empty() || !state.loaded_mode.empty())
		throw std::runtime_error("Saved non-weapon has committed ammunition modes");
}
} // namespace phoenix
