module;
#include <flecs.h>
#include <functional>
#include <chrono>
#include <charconv>
#include <iterator>
#include <type_traits>
#include <string_view>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <array>
#include <variant>
#include <set>
#include <stdexcept>
#include <cstdint>
#include <map>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <tuple>
#include <vector>

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::validate_native_payload_layouts() const -> void
{
	const auto projectile = [&](const ProjectileDefinition &definition)
	{
		(void)payload_capture_layout(definition.payloads);
		(void)payload_capture_layout(definition.destruction.payloads);
	};
	for (const auto &[id, definition] : definitions)
	{
		if (definition.destruction)
			(void)payload_capture_layout(definition.destruction->payloads);
		if (definition.combat)
		{
			(void)payload_capture_layout(definition.combat->destruction.payloads);
			for (const auto &system : definition.combat->systems)
			{
				if (system.kind == OperatingSystem::Weapon || system.kind == OperatingSystem::Magazine)
					projectile(system.ammunition().projectile);
				if (system.kind == OperatingSystem::Magazine)
					for (const auto &[resource, ammunition] : system.get<MagazineDefinition>().ammunition_profiles)
						projectile(ammunition.projectile);
			}
		}
		for (const auto &[equipment, directed] : definition.directed)
			for (const auto &mode : directed.modes)
			{
				projectile(mode.ammunition.projectile);
				(void)payload_capture_layout(mode.payloads);
			}
	}
}

auto Runtime::volume_hit(fixed::Vec a, fixed::Vec b, std::int64_t za, std::int64_t zb, CollisionBody target,
                         fixed::Vec target_start, std::int64_t tz_start, std::int64_t tz_end, std::int64_t half_height,
                         std::int64_t radius) const -> std::optional<std::int64_t>
{
	return ordnance_volume_hit(a, b, za, zb, target, target_start, tz_start, tz_end, half_height, radius);
}

auto Runtime::blast_occluded(fixed::Vec origin, std::int64_t z, fixed::Vec end, std::int64_t ez, EntityId ignore) const
    -> bool
{
	bool blocked = false;
	bodies.each(
	    [&](flecs::entity e, const StableId &id, const Definition &, const Pose &pose, const Motion &)
	    {
		    if (id.id == ignore || combat_dead(e))
			    return;
		    CollisionBody b;
		    static_cast<Pose &>(b) = pose;
		    static_cast<Collider &>(b) = e.get<Collider>();
		    const auto extent = definitions.at(e.get<Definition>().name).half_height;
		    if (const auto hit = volume_hit(origin, end, z, ez, b, pose.position, pose.altitude, pose.altitude, extent);
		        hit && *hit > 0 && *hit < fixed::unit)
			    blocked = true;
	    });
	return blocked;
}

auto Runtime::blast_exposure(fixed::Vec origin, std::int64_t altitude, flecs::entity target) const -> BlastExposure
{
	return blast_surface_exposure(origin, altitude, target.get<Collider>(), target.get<Pose>(),
	                              definitions.at(target.get<Definition>().name).half_height);
}

auto Runtime::emit_blast(fixed::Vec origin, std::int64_t altitude, const DestructionDefinition &d, EntityId source,
                         const std::string &weapon, const std::vector<RuleCapturedFact> *origin_captures) -> void
{
	if (!d.visual.empty() || !d.sound.empty())
	{
		if (blast_visuals.size() >= 8192)
			blast_visuals.erase(blast_visuals.begin());
		blast_visuals.push_back({next_blast++, origin, altitude, static_cast<std::int64_t>(ecs.get<Clock>().tick), d});
	}
	if ((!d.damage && d.payloads.empty()) || !d.radius)
		return;
	const auto captures = origin_captures ? *origin_captures : capture_payload_rule_facts(source, weapon, d.payloads);
	for (const auto &[id, ship] : combat_rows())
	{
		if (combat_dead(ship))
			continue;
		const auto centre = ship.get<Pose>().position;
		const auto exposure = blast_exposure(origin, altitude, ship);
		const auto distance = exposure.distance;
		if (distance >= d.radius || blast_occluded(origin, altitude, exposure.point, exposure.altitude, id))
			continue;
		Projectile effect;
		effect.source = source;
		effect.weapon = weapon;
		effect.payloads = d.payloads;
		effect.rule_captures = captures;
		effect.heading = combat_bearing(centre - origin);
		effect.damage = d.damage * (d.radius - distance) / d.radius;
		effect.penetration = 1000000;
		effect.definition.deposition = 1000000;
		effect.definition.shield_bypass = d.shield_bypass;
		const auto &shape = ship.get<Collider>();
		const auto extent = shape.box ? shape.half_width + shape.half_length : shape.radius;
		const auto axis = fixed::direction(effect.heading);
		penetrate(effect, ship, centre - fixed::scale(axis, extent + 1), centre + fixed::scale(axis, extent + 1));
	}
	if (d.affects_missiles)
		for (auto row : projectile_rows())
		{
			auto &p = row.get_mut<Projectile>();
			if (!p.definition.durability || row.get_mut<Destructible>().durability <= 0)
				continue;
			const auto planar = fixed::length(p.position - origin), dz = p.altitude - altitude;
			const auto distance =
			    static_cast<std::int64_t>(fixed::sqrt(static_cast<std::uint64_t>(planar * planar + dz * dz)));
			if (distance < d.radius && !blast_occluded(origin, altitude, p.position, p.altitude, 0))
			{
				Projectile payload;
				payload.source = source;
				payload.weapon = weapon;
				payload.payloads = d.payloads;
				payload.rule_captures = captures;
				const PayloadHitContext hit{p.id, PayloadOutcomeKind::Entity, -1, {}, 0, {}, true};
				const auto amount =
				    damage_native_durability(row, d.damage * (d.radius - distance) / d.radius, hit, nullptr, &payload);
				queue_payload_outcome(payload, hit, amount);
			}
		}
	bodies.each(
	    [&](flecs::entity e, const StableId &id, const Definition &, const Pose &pose, const Motion &)
	    {
		    auto *v = e.try_get_mut<Destructible>();
		    if (!v || v->durability <= 0 || e.has<CombatStructure>())
			    return;
		    const auto exposure = blast_exposure(origin, altitude, e);
		    const auto distance = exposure.distance;
		    if (distance < d.radius && !blast_occluded(origin, altitude, exposure.point, exposure.altitude, id.id))
		    {
			    Projectile payload;
			    payload.source = source;
			    payload.weapon = weapon;
			    payload.payloads = d.payloads;
			    payload.rule_captures = captures;
			    const auto amount = damage_native_durability(e, d.damage * (d.radius - distance) / d.radius, {id.id},
			                                                 nullptr, &payload);
			    queue_payload_outcome(payload, {id.id}, amount);
		    }
	    });
}

auto Runtime::emit_native_destroy_rules(flecs::entity object, const RuleActor &actor,
                                        const std::vector<NativeRuleSource> *snapshot) -> void
{
	auto *native = object.try_get_mut<Destructible>();
	if (!native || native->rule_notified)
		return;
	const auto sources = snapshot ? std::vector<NativeRuleSource>{} : rule_sources(object, true);
	// Notification belongs to first native death, independently of later blast
	// resolution. Queue-pressure failure is still a once-only delivered receipt.
	native->rule_notified = true;
	auto event = damage_rule_context(rule_hit_context(actor), 0);
	// Native destruction has no persisted killer-credit field. Its instigator
	// is the retiring native actor; each source view retains its own provenance.
	event.instigator = actor;
	visit_damage_rule_bindings(
	    object, RuleTrigger::Destroy, event, nullptr,
	    [&](const auto &source, const auto &rule, const auto &context)
	    {
		    (void)enqueue_rule(rule.id, source, context, RuleEventPhase::Payload,
		                       static_cast<std::int64_t>(ecs.get<Clock>().tick) + 1);
	    },
	    snapshot ? snapshot : &sources);
}

auto Runtime::destroy_missile(flecs::entity row, bool warhead) -> void
{
	auto &p = row.get_mut<Projectile>();
	if (p.life <= 0)
		return;
	auto &component = row.get_mut<Destructible>();
	if (component.processed)
		return;
	const auto sources = rule_sources(row, true);
	component.destroyed_tick = ecs.get<Clock>().tick;
	emit_native_destroy_rules(row, {p.id, {}, {}, true}, &sources);
	if (const auto effect = retire_projectile_native(p, component, warhead))
		emit_blast(p.position, p.altitude, *effect, p.source, p.weapon,
		           warhead ? &p.rule_captures : &p.destruction_rule_captures);
}

auto Runtime::resolve_destructions() -> void
{
	// Once-only components bound chain work by the finite entity/projectile roster.
	bool changed = true;
	std::size_t iterations = 0;
	while (changed && iterations++ < 16384)
	{
		changed = false;
		for (const auto &[id, e] : entities)
			if (auto *d = e.try_get_mut<Destructible>(); d && !d->processed)
			{
				if (d->destroyed_tick < 0)
					continue;
				emit_native_destroy_rules(e, {id});
				const bool blast = retire_destroyed_structure(*d, id);
				changed = true;
				if (blast)
				{
					emit_blast(e.get<Pose>().position, altitude(id), d->definition, id);
				}
			}
		for (auto e : projectile_rows())
		{
			const auto &p = e.get<Projectile>();
			if (p.definition.durability && e.get<Destructible>().durability <= 0 && p.life > 0)
			{
				destroy_missile(e, false);
				changed = true;
			}
		}
	}
	for (auto e : projectile_rows())
		if (projectile_finished(e.get<Projectile>()))
			e.destruct();
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	std::erase_if(blast_visuals,
	              [&](const auto &v)
	              {
		              return blast_visual_finished(v, tick);
	              });
}

auto Runtime::fire_beam(flecs::entity ship, flecs::entity child, fixed::Vec origin, std::int64_t bearing) -> void
{
	auto &m = child.get_mut<CombatMechanism>();
	const auto &d = m.definition;
	m.get<WeaponState>().beam_tick = ecs.get<Clock>().tick;
	m.get<WeaponState>().beam_end = origin + fixed::scale(fixed::direction(bearing), d.get<WeaponDefinition>().range);
	ConsoleContacts permitted;
	const auto prepared = directed_operations.find({ship.get<StableId>().id, d.id});
	const auto aim = prepared == directed_operations.end() ? weapon_contact(ship, m, permitted)
	                                                       : std::optional<DirectedAim>{prepared->second.aim};
	const auto from_z = ship.get<Pose>().altitude;
	const auto planar_distance =
	    aim ? std::max<std::int64_t>(1, fixed::length(fixed::Vec{aim->body.x, aim->body.y} - origin)) : 1;
	const auto end_z =
	    aim ? from_z + (aim->altitude - from_z) * d.get<WeaponDefinition>().range / planar_distance : from_z;
	std::int64_t nearest = fixed::unit + 1;
	EntityId victim = 0;
	bodies.each(
	    [&](flecs::entity e, const StableId &id, const Definition &, const Pose &pose, const Motion &)
	    {
		    if (id.id == ship.get<StableId>().id || combat_dead(e))
			    return;
		    CollisionBody target;
		    static_cast<Pose &>(target) = pose;
		    static_cast<Collider &>(target) = e.get<Collider>();
		    if (auto hit = volume_hit(origin, m.get<WeaponState>().beam_end, from_z, end_z, target, pose.position,
		                              pose.previous_altitude, pose.altitude,
		                              definitions.at(e.get<Definition>().name).half_height);
		        hit && std::pair{*hit, id.id} < std::pair{nearest, victim})
		    {
			    nearest = *hit;
			    victim = id.id;
		    }
	    });
	const auto full_end = m.get<WeaponState>().beam_end;
	if (victim)
		m.get<WeaponState>().beam_end = origin + fixed::Vec{(full_end.x - origin.x) * nearest / fixed::unit,
		                                                    (full_end.y - origin.y) * nearest / fixed::unit};
	const auto &grant = ship.get<ShipResources>().grants.at(child.get<ResourceBinding>().slot);
	auto &directed = child.get_mut<DirectedRuntime>();
	const auto funded =
	    d.get<WeaponDefinition>().beam_damage * fault_effectiveness(ship, d.id) * grant.supply_permille +
	    directed.beam_funding_remainder;
	directed.beam_funding_remainder = funded % 1000;
	m.get<WeaponState>().beam_remainder += funded / 1000;
	const auto damage = m.get<WeaponState>().beam_remainder / (rate * 1000);
	m.get<WeaponState>().beam_remainder %= rate * 1000;
	++m.get<WeaponState>().shots;
	psychic_weapon_emission(ship.get<StableId>().id);
	if (damage)
		ghost_damage(ship.get<StableId>().id, origin, m.get<WeaponState>().beam_end, from_z,
		             from_z + (end_z - from_z) * std::min(nearest, fixed::unit) / fixed::unit);
	const auto payloads = capture_directed_payloads(ship, m, {});
	const auto captures = capture_payload_rule_facts(ship.get<StableId>().id, d.id, payloads);
	if (!victim || (!damage && payloads.empty()))
		return;
	auto target = entities.at(victim);
	if (target.has<CombatStructure>())
	{
		Projectile p;
		p.source = ship.get<StableId>().id;
		p.weapon = d.id;
		p.payloads = payloads;
		p.rule_captures = captures;
		p.damage = damage;
		p.continuous = true;
		p.bypass_remainder = m.get<WeaponState>().bypass_remainder;
		p.penetration = d.ammunition().projectile.penetration;
		p.definition = d.ammunition().projectile;
		p.heading = bearing;
		penetrate(p, target, origin, full_end);
		m.get<WeaponState>().bypass_remainder = p.bypass_remainder;
	}
	else if (auto *v = target.try_get_mut<Destructible>(); v && v->durability > 0)
	{
		Projectile payload;
		payload.source = ship.get<StableId>().id;
		payload.weapon = d.id;
		payload.payloads = payloads;
		payload.rule_captures = captures;
		const auto actual = damage_native_durability(target, damage, {victim}, nullptr, &payload);
		queue_payload_outcome(payload, {victim}, actual);
	}
}

auto Runtime::advance_ordnance(Projectile &p) -> void
{
	std::optional<OrdnanceTarget> target;
	if (entities.contains(p.target))
		target = OrdnanceTarget{entities.at(p.target).get<Pose>().position, altitude(p.target),
		                        !combat_dead(entities.at(p.target))};
	advance_projectile(p, target);
}

auto Runtime::step_ordnance() -> void
{
	if (duel_ended())
	{
		for (auto e : projectile_rows())
			e.destruct();
		return;
	}
	using Previous = OrdnancePrevious;
	std::map<std::uint64_t, Previous> previous;
	auto rows = projectile_rows();
	for (auto row : rows)
	{
		auto &p = row.get_mut<Projectile>();
		previous[p.id] = {p.position, p.altitude};
		advance_ordnance(p);
	}
	std::vector<flecs::entity> target_entities;
	std::vector<OrdnanceBodySweep> targets;
	bodies.each(
	    [&](flecs::entity e, const StableId &id, const Definition &, const Pose &pose, const Motion &)
	    {
		    if (combat_dead(e))
			    return;
		    CollisionBody body;
		    static_cast<Pose &>(body) = pose;
		    static_cast<Collider &>(body) = e.get<Collider>();
		    const auto start = step_starts.contains(id.id) ? step_starts.at(id.id) : pose.position;
		    target_entities.push_back(e);
		    targets.push_back({id.id, body, start, definitions.at(e.get<Definition>().name).half_height});
	    });
	std::vector<flecs::entity> missiles;
	std::vector<OrdnanceMissileSweep> missile_geometry;
	for (auto row : rows)
	{
		const auto &p = row.get<Projectile>();
		if (!p.definition.durability)
			continue;
		missiles.push_back(row);
		missile_geometry.push_back({p.id, p.source, p.position, p.altitude, p.definition.radius, previous.at(p.id)});
	}
	OrdnanceContactIndex contact_index(std::move(targets), std::move(missile_geometry));
	for (auto row : rows)
	{
		auto &p = row.get_mut<Projectile>();
		if (p.life <= 0 || (p.definition.durability && row.get_mut<Destructible>().durability <= 0))
			continue;
		const auto from = previous.at(p.id);
		struct Hit
		{
			std::int64_t time;
			EntityId id;
			flecs::entity entity;
			bool missile;
		};
		auto contacts = contact_index.contacts(p, from);
		std::erase_if(
		    contacts,
		    [&](const auto &contact)
		    {
			    const auto entity = contact.missile ? missiles.at(contact.index) : target_entities.at(contact.index);
			    return contact.missile ? entity.template get<Destructible>().durability <= 0 : combat_dead(entity);
		    });
		for (const auto &contact : contacts)
		{
			const Hit hit{contact.time, contact.id,
			              contact.missile ? missiles.at(contact.index) : target_entities.at(contact.index),
			              contact.missile};
			if (p.life <= 0 || ((!p.damage || !p.penetration) && p.definition.damage > 0))
				break;
			const auto impact = from.position + fixed::Vec{(p.position.x - from.position.x) * hit.time / fixed::unit,
			                                               (p.position.y - from.position.y) * hit.time / fixed::unit};
			const auto z = from.altitude + (p.altitude - from.altitude) * hit.time / fixed::unit;
			if (p.damage > 0)
				ghost_damage(p.source, from.position, impact, from.altitude, z, p.definition.radius);
			if (p.definition.durability && p.definition.blast_radius)
			{
				p.position = impact;
				p.altitude = z;
				destroy_missile(row, true);
				break;
			}
			if (hit.missile)
			{
				auto &m = hit.entity.get_mut<Projectile>();
				if (hit.entity.get_mut<Destructible>().durability <= 0)
					continue;
				const auto old = previous.at(m.id);
				CollisionBody reverse_target;
				reverse_target.position = old.position;
				reverse_target.radius = m.definition.radius;
				reverse_target.mass = 1;
				const auto reverse =
				    volume_hit(p.position, from.position, p.altitude, from.altitude, reverse_target, m.position,
				               m.altitude, old.altitude, m.definition.radius, p.definition.radius);
				const auto exit = reverse ? fixed::unit - *reverse : fixed::unit;
				if (exit < fixed::unit)
					p.missile_hits.insert(m.id);
				const auto relative = (p.position - from.position) - (m.position - old.position);
				const auto dz = (p.altitude - from.altitude) - (m.altitude - old.altitude);
				const auto path = static_cast<std::int64_t>(
				    fixed::sqrt(static_cast<std::uint64_t>(fixed::dot(relative, relative) + dz * dz)));
				const auto distance = path * std::max<std::int64_t>(0, exit - hit.time) / fixed::unit;
				const auto deposited = deposit_material(p, distance, m.definition.resistance);
				const PayloadHitContext context{m.id, PayloadOutcomeKind::Entity, -1, {}, 0, {}, true};
				const auto actual = damage_native_durability(hit.entity, deposited, context, nullptr, &p);
				queue_payload_outcome(p, context, actual);
				if (!p.definition.damage)
					p.life = 0;
			}
			else if (hit.entity.has<CombatStructure>())
			{
				const auto start =
				    step_starts.contains(hit.id) ? step_starts.at(hit.id) : hit.entity.get<Pose>().position;
				penetrate(p, hit.entity, from.position + (hit.entity.get<Pose>().position - start), p.position);
			}
			else
			{
				if (auto *d = hit.entity.try_get_mut<Destructible>(); d && d->durability > 0)
				{
					const auto actual = damage_native_durability(hit.entity, p.damage, {hit.id}, nullptr, &p);
					queue_payload_outcome(p, {hit.id}, actual);
				}
				p.damage = 0;
				p.penetration = 0;
				p.life = 0;
			}
		}
		if (p.damage > 0)
			ghost_damage(p.source, from.position, p.position, from.altitude, p.altitude, p.definition.radius);
		if (p.life > 0 && --p.life == 0 && p.definition.durability)
		{
			p.life = 1;
			destroy_missile(row, false);
		}
		if (std::abs(p.position.x) > fixed::unit || std::abs(p.position.y) > fixed::unit)
			p.life = 0;
	}
}
} // namespace phoenix::world_detail
