module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.gameplay.weapons;
namespace phoenix
{
const AmmunitionDefinition &native_magazine_ammunition(const MagazineDefinition &d, const std::string &resource)
{
	const auto at = d.ammunition_profiles.find(resource);
	return at == d.ammunition_profiles.end() ? d.ammunition : at->second;
}
const AmmunitionDefinition &native_weapon_ammunition(const CombatMechanism &weapon, const WeaponMagazines &magazines,
                                                     std::int64_t index)
{
	const auto &state = weapon.get<WeaponState>();
	const auto selected = index >= 0                   ? index
	                      : state.loaded_magazine >= 0 ? state.loaded_magazine
	                                                   : state.selected_magazine;
	const auto &mag = magazines.at(weapon.definition.get<WeaponDefinition>().magazines.at(selected));
	const auto &resource = state.loaded_magazine == selected ? state.loaded_resource : mag.stock->selected();
	return native_magazine_ammunition(*mag.definition, resource);
}
std::int64_t native_weapon_load_duration(const CombatMechanism &m, const DirectedRuntime *state,
                                         const DirectedDefinition *directed, const WeaponMagazines &magazines,
                                         std::int64_t index)
{
	if (m.definition.get<WeaponDefinition>().magazines.empty())
		return 0;
	const auto &ammunition = native_weapon_ammunition(m, magazines, index);
	if (ammunition.load_ticks)
		return ammunition.load_ticks;
	if (state)
	{
		const auto &selected = m.get<WeaponState>();
		const auto &mode_id = selected.loaded_magazine >= 0 && (index < 0 || index == selected.loaded_magazine)
		                          ? state->loaded_mode
		                          : state->loading_mode;
		if (!mode_id.empty())
		{
			const auto &modes = directed->modes;
			const auto mode = std::find_if(modes.begin(), modes.end(),
			                               [&](const auto &v)
			                               {
				                               return v.id == mode_id;
			                               });
			if (mode == modes.end() || mode->operation != DirectedOperationKind::Launch)
				throw std::runtime_error("Invalid committed loading mode");
			return mode->ammunition.load_ticks;
		}
	}
	return m.definition.ammunition().load_ticks;
}
const ProjectileDefinition &native_weapon_payload(const CombatMechanism &m, const WeaponMagazines &magazines)
{
	if (!m.definition.get<WeaponDefinition>().magazines.empty())
	{
		const auto &p = native_weapon_ammunition(m, magazines).projectile;
		if (!p.visual.empty())
			return p;
	}
	return m.definition.ammunition().projectile;
}
void reset_weapon_tick(WeaponState &weapon, DirectedRuntime *directed)
{
	weapon.ready = false;
	if (weapon.cooldown > 0)
		--weapon.cooldown;
	if (directed)
	{
		directed->ready = false;
		directed->blocker = "Target or operation not ready";
	}
}
void advance_weapon_ammunition(CombatMechanism &m, DirectedRuntime &directed, const DirectedDefinition &definition,
                               bool launch_mode, const WeaponMagazines &magazines)
{
	auto &w = m.get<WeaponState>();
	const auto &d = m.definition;
	if (w.unloading)
	{
		if (--w.unloading == 0)
		{
			auto &stock = *magazines.at(d.get<WeaponDefinition>().magazines.at(w.loaded_magazine)).stock;
			if (!stock.receive_reserved("weapon:" + d.id))
				throw std::runtime_error("Missing loaded ammunition return commitment");
			w.loaded_resource.clear();
			w.loaded_quantity = 0;
			w.loaded_magazine = -1;
			directed.loaded_mode.clear();
		}
		return;
	}
	if (!launch_mode)
		return;
	if (native_weapon_load_duration(m, &directed, &definition, magazines) && w.loaded_magazine < 0 &&
	    !d.get<WeaponDefinition>().magazines.empty())
	{
		const auto &mag = magazines.at(d.get<WeaponDefinition>().magazines.at(w.selected_magazine));
		auto &stock = *mag.stock;
		auto quantity = d.get<WeaponDefinition>().ammo_use;
		if (!directed.loading_mode.empty())
			for (const auto &mode : definition.modes)
				if (mode.id == directed.loading_mode)
					quantity = mode.ammo_use;
		if (mag.operating && stock.available(stock.selected()) >= quantity)
		{
			if (!w.loading)
				directed.loading_mode = definition.modes.at(directed.operating_mode).id;
			if (++w.loading < native_weapon_load_duration(m, &directed, &definition, magazines))
				return;
			StockTransaction transaction;
			const auto resource = stock.selected();
			if (!transaction.consume(stock, resource, quantity) ||
			    !transaction.reserve_capacity(stock, "weapon:" + d.id, resource, quantity) || !transaction.commit())
				return;
			w.loaded_resource = resource;
			w.loaded_quantity = quantity;
			w.loaded_magazine = w.selected_magazine;
			w.loading = 0;
			directed.loaded_mode = directed.loading_mode;
			directed.loading_mode.clear();
			if (quantity != d.get<WeaponDefinition>().ammo_use)
				w.unloading =
				    std::max<std::int64_t>(1, native_weapon_load_duration(m, &directed, &definition, magazines));
		}
	}
}
bool commit_weapon_launch(WeaponState &weapon, DirectedRuntime &directed, PreparedDirectedOperation &prepared,
                          StockStore &stock, const std::string &id, std::size_t live_projectiles)
{
	if (live_projectiles >= 8192)
		return false;
	if (prepared.loaded)
	{
		if (weapon.loaded_magazine != weapon.selected_magazine || !stock.release_capacity("weapon:" + id))
			return false;
		weapon.loaded_magazine = -1;
		weapon.loaded_resource.clear();
		weapon.loaded_quantity = 0;
		directed.loaded_mode.clear();
	}
	else if (!stock.consume_claim("directed-launch:" + id))
		return false;
	prepared.stock_claim = false;
	weapon.cooldown = prepared.cycle_ticks;
	++weapon.shots;
	return true;
}
Projectile create_launched_projectile(PreparedDirectedOperation &prepared, ProjectileLaunch launch,
                                      std::uint64_t &random, std::uint64_t &next_projectile)
{
	random = random * 6364136223846793005ULL + 1;
	const auto &payload = prepared.projectile;
	auto bearing = prepared.bearing;
	if (payload.spread)
		bearing = combat_angle(bearing + static_cast<std::int64_t>((random >> 16) % (payload.spread * 2 + 1)) -
		                       payload.spread);
	Projectile p;
	p.definition = payload;
	p.payloads = std::move(prepared.payloads);
	p.id = next_projectile++;
	p.source = launch.source;
	p.weapon = std::move(launch.weapon);
	p.target = launch.target;
	p.position = prepared.origin;
	p.altitude = launch.altitude;
	p.launch_altitude = p.altitude;
	p.heading = bearing;
	p.base_heading = bearing;
	p.velocity = fixed::scale(fixed::direction(bearing), payload.speed) + launch.velocity;
	p.life = payload.lifetime;
	p.penetration = payload.penetration;
	p.damage = payload.damage;
	p.random = random;
	p.bearing_launch = prepared.aim.bearing.has_value();
	p.seeker_altitude = p.launch_altitude;
	p.initial_distance =
	    prepared.aim.bearing
	        ? launch.range
	        : std::max<std::int64_t>(
	              1, fixed::length(fixed::Vec{prepared.aim.body.x, prepared.aim.body.y} - prepared.origin));
	p.vz = (prepared.aim.altitude - p.altitude +
	        prepared.aim.vz * p.initial_distance / std::max<std::int64_t>(1, payload.speed)) *
	       payload.speed / p.initial_distance;
	return p;
}
} // namespace phoenix
