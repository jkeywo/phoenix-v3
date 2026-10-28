module;
#include "native_headers.hpp"

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::psychic_weapon_emission(EntityId ship) -> void
{
	weapon_emissions.record(ship, ecs.get<Clock>().tick);
}

auto Runtime::emission(EntityId ship) const -> std::int64_t
{
	const auto tick = ecs.get<Clock>().tick;
	const auto entity = entities.at(ship);
	return native_psychic_emission(entity, ship, combat_dead(entity), weapon_emissions.recent(ship, tick),
	                               conversations, tick);
}

auto Runtime::choir_officer(EntityId ship, const std::string &duty) const -> const OfficerDefinition *
{
	const auto &p = definition(ship).personnel;
	const auto &s = entities.at(ship).get<PersonnelState>();
	for (std::size_t i = 0; i < s.officers.size(); ++i)
		if (s.officers[i].duty == duty && s.officers[i].present && officer_healthy(s.officers[i]))
			return &p.officers[i];
	return nullptr;
}

auto Runtime::choir_assignment(EntityId ship, const OfficerDefinition &officer, const PersonnelDuty *duty) const -> bool
{
	if (!duty || duty->choir.empty())
		return true;
	const auto &compatible = definition(ship).psychic.choirs.at(duty->choir).powers;
	return std::any_of(officer.abilities.begin(), officer.abilities.end(),
	                   [&](const auto &ability)
	                   {
		                   return compatible.contains(ability);
	                   });
}

auto Runtime::psychic_execution(EntityId ship, const PsychicContribution &contribution) const
    -> const AbilityGrantState *
{
	const auto *runtime = entities.contains(ship) ? entities.at(ship).try_get<AbilityRuntime>() : nullptr;
	return find_psychic_execution(runtime, contribution);
}

auto Runtime::contribution_power(EntityId ship, const PsychicContribution &contribution) const
    -> const PsychicPowerDefinition &
{
	return definition(ship).psychic.power(contribution.ability);
}

auto Runtime::refresh_psychic_work() -> void
{
	for (auto child : ordered_equipment(choir_query))
	{
		const auto ship = child.parent().get<StableId>().id;
		refresh_psychic_contribution_work(child.get_mut<PsychicChoirState>(),
		                                  entities.at(ship).try_get<AbilityRuntime>(), definition(ship).psychic,
		                                  departed(ship));
	}
}

auto Runtime::sync_psychic_contributions() -> void
{
	struct Targets final : PsychicTargetPort
	{
		const Impl &world;
		explicit Targets(const Impl &world) : world(world)
		{
		}
		std::optional<Pose> projection_target_pose(EntityId target) const override
		{
			return world.entities.contains(target) ? std::optional<Pose>{world.entities.at(target).get<Pose>()}
			                                       : std::nullopt;
		}
	} targets{*this};
	for (auto child : ordered_equipment(choir_query))
	{
		const auto ship = child.parent().get<StableId>().id;
		reconcile_psychic_contributions(child.get_mut<PsychicChoirState>(), ship, child.get<Equipment>().id,
		                                entities.at(ship).try_get<AbilityRuntime>(), definition(ship).psychic, targets);
	}
	refresh_psychic_work();
	// Stops/removals can commit between ticks. End only receipts whose execution
	// link was retired, not those with transient zero output during allocation.
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	for (auto &[id, reception] : ghost_receptions.records())
		if (reception.ended < 0)
		{
			bool linked = false;
			const auto *execution = ability_execution(reception.source, reception.grant);
			if (execution && execution->apparatus == reception.choir && execution->definition == reception.power &&
			    execution->activation == reception.activation &&
			    (execution->phase == "active" || (execution->phase == "paused" && execution->resume_phase == "active")))
			{
				const auto apparatus = instance(entities.at(reception.source), reception.choir);
				const auto *state = apparatus ? apparatus->entity.try_get<PsychicChoirState>() : nullptr;
				if (state)
				{
					const auto contribution = state->contributions.find(reception.grant);
					linked = contribution != state->contributions.end() &&
					         contribution->second.activation == reception.activation &&
					         contribution->second.ability == reception.power;
				}
			}
			if (!linked)
				ghost_receptions.end_unlinked(id, tick);
		}
	refresh_psychic_reception();
	refresh_concealment();
}

auto Runtime::psychic_coverage(EntityId ship, const ChoirDefinition &c, const PsychicContribution &s,
                               EntityId receiver) const -> bool
{
	if (!s.work || !entities.contains(receiver) || departed(receiver) || combat_dead(entities.at(receiver)))
		return false;
	if (receiver == ship)
		return true;
	const auto *execution = psychic_execution(ship, s);
	if (!execution || !entities.contains(execution->target))
		return false;
	const auto origin = entities.at(ship).get<Pose>().position;
	const auto delta = entities.at(receiver).get<Pose>().position - origin;
	const auto aim = fixed::Vec{s.aim_x, s.aim_y} - origin;
	return distance_3d(ship, receiver) <= c.range &&
	       scenario_sensor_visible(regions, {origin.x, origin.y}, {origin.x + delta.x, origin.y + delta.y}, c.range) &&
	       (c.arc == 360000 || std::abs(combat_delta(combat_bearing(delta), combat_bearing(aim))) <= c.arc / 2);
}

auto Runtime::compute_concealment(EntityId target) const -> std::int64_t
{
	std::int64_t strength = 0;
	if (!entities.contains(target))
		return 0;
	for (const auto &[id, c] : definition(target).psychic.choirs)
		for (const auto &[grant, s] : instance(entities.at(target), id)->entity.get<PsychicChoirState>().contributions)
			if (s.work && contribution_power(target, s).kind == "cloak")
				strength = std::max(strength, s.work);
	return strength ? strength * (1000 - emission(target)) / 1000 : 0;
}

auto Runtime::refresh_concealment() -> void
{
	psychic_interference.begin_concealment();
	for (const auto &[ship, entity] : entities)
		if (entity.has<EquipmentIndex>())
			psychic_interference.set_concealment(ship, compute_concealment(ship));
}

auto Runtime::concealment(EntityId target) const -> std::int64_t
{
	return psychic_interference.concealment(target);
}

auto Runtime::dispel_ghost(const GhostReception &g) -> void
{
	if (ghost_receptions.dispel(g.id, ecs.get<Clock>().tick))
		record_sensor_dispel(g);
}

auto Runtime::ghost_live(const GhostReception &g) const -> bool
{
	return psychic_ghost_live(g, ecs.get<Clock>().tick);
}

auto Runtime::refresh_psychic_reception() -> void
{
	psychic_interference.begin_reception();
	std::vector<PsychicInterferenceSource> sources;
	for (auto child : ordered_equipment(choir_query))
	{
		const auto ship = child.parent().get<StableId>().id;
		const auto &choir = definition(ship).psychic.choirs.at(child.get<Equipment>().id);
		for (const auto &[grant, contribution] : child.get<PsychicChoirState>().contributions)
			if (const auto *execution = psychic_execution(ship, contribution); execution && contribution.work > 0)
				sources.push_back({ship, &grant, &choir, &contribution, execution});
	}
	struct Interference final : PsychicInterferencePort
	{
		const Impl &world;
		explicit Interference(const Impl &world) : world(world)
		{
		}
		bool is_ward(const PsychicInterferenceSource &source) const override
		{
			return world.contribution_power(source.ship, *source.contribution).kind == "ward";
		}
		bool covers(const PsychicInterferenceSource &source, EntityId receiver) const override
		{
			return world.psychic_coverage(source.ship, *source.choir, *source.contribution, receiver);
		}
	} interference{*this};
	for (const auto &[receiver, entity] : entities)
		if (entity.has<EquipmentIndex>())
			psychic_interference.observe(receiver, sources, interference);
}

auto Runtime::strongest_scrambler(EntityId receiver) const -> PsychicSource
{
	return psychic_interference.strongest(receiver);
}

auto Runtime::advance_psychic() -> void
{
	refresh_psychic_work();
	refresh_psychic_reception();
	refresh_concealment();
	const auto tick = ecs.get<Clock>().tick;
	weapon_emissions.expire(tick,
	                        [this](EntityId ship)
	                        {
		                        return entities.contains(ship);
	                        });
	for (auto child : ordered_equipment(choir_query))
		if (child.has<PsychicChoirState>())
		{
			const auto ship = child.parent().get<StableId>().id;
			const auto &c = definition(ship).psychic.choirs.at(child.get<Equipment>().id);
			auto &contributions = child.get_mut<PsychicChoirState>().contributions;
			for (auto &[grant, s] : contributions)
			{
				if (!s.work || s.shapes.empty())
					continue;
				const auto &p = contribution_power(ship, s);
				advance_psychic_shapes(s, p);
				for (const auto &[receiver, entity] : entities)
				{
					if (receiver == ship || !entity.has<EquipmentIndex>() ||
					    strongest_scrambler(receiver) != std::tuple{ship, c.id, grant})
						continue;
					for (std::size_t n = 0; n < s.shapes.size(); ++n)
					{
						const auto &shape = s.shapes[n];
						bool visible = false;
						for (const auto &[sensor, d] : definition(receiver).sensors)
							if (sensor_covers(receiver, sensor, {shape.pose.position.x, shape.pose.position.y}, true))
								visible = true;
						if (!visible)
							continue;
						ghost_receptions.observe({receiver, ship, c.id, grant, s.ability, s.activation, n}, shape, tick,
						                         next_entity);
					}
				}
			}
		}
	ghost_receptions.end_unseen(tick);
	// Keep rejection receipts for the current activation until the original fade.
	for (auto it = ghost_receptions.records().begin(); it != ghost_receptions.records().end();)
	{
		const auto &g = (it++)->second;
		if (!entities.contains(g.source) || !definition(g.source).psychic.choirs.contains(g.choir))
		{
			ghost_receptions.retire(g.id, tick, false, nullptr);
			continue;
		}
		const auto *execution = ability_execution(g.source, g.grant);
		ghost_receptions.retire(g.id, tick, true, execution);
	}
}

auto Runtime::ghost_body(const GhostReception &g) const -> BodyView
{
	return psychic_ghost_body(g, definition(g.source).psychic.power(g.power).apparent_class);
}

auto Runtime::ghost_damage(EntityId observer, fixed::Vec from, fixed::Vec to, std::int64_t from_z, std::int64_t to_z,
                           std::int64_t radius) -> void
{
	for (auto &[id, g] : ghost_receptions.records())
		if (g.observer == observer && ghost_live(g))
		{
			const auto &p = definition(g.source).psychic.power(g.power);
			CollisionBody body;
			body.position = {g.last.pose.position.x, g.last.pose.position.y};
			body.altitude = g.last.pose.altitude;
			body.radius = p.radius;
			if (volume_hit(from, to, from_z, to_z, body, body.position, g.last.pose.altitude, g.last.pose.altitude,
			               p.radius, radius))
				dispel_ghost(g);
		}
}

auto Runtime::psychic_board(const ConsoleAddress &address) const -> Json
{
	Json out = {{"choirs", Json::array()}, {"fading", Json::array()}};
	std::map<std::string, const AbilityGrantState *> by_control;
	if (const auto *runtime = entities.at(address.ship).try_get<AbilityRuntime>())
		for (const auto &[id, execution] : runtime->grants)
		{
			const auto found = by_control.find(execution.control);
			if (found == by_control.end() || execution.phase == "active")
				by_control[execution.control] = &execution;
		}
	for (const auto &[id, c] : definition(address.ship).psychic.choirs)
	{
		Json seats = Json::array();
		const auto &native = instance(entities.at(address.ship), id)->entity.get<PsychicChoirState>().contributions;
		for (const auto &binding : c.seats)
		{
			if (!personnel_owns(address, personnel_duty(address.ship, binding.duty)->department))
				continue;
			const auto *officer = choir_officer(address.ship, binding.duty);
			const auto found = by_control.find(binding.control);
			const auto *execution = found == by_control.end() ? nullptr : found->second;
			const auto contribution = execution ? native.find(execution->id) : native.end();
			const auto &controller = entities.at(address.ship).get<EntityControls>().controllers.at(binding.controller);
			const auto work = contribution == native.end() ? 0 : contribution->second.work;
			const auto label =
			    execution && definition(address.ship).psychic.has_power(execution->definition)
			        ? definition(address.ship).abilities.catalog->definitions.at(execution->definition).label
			        : std::string{};
			seats.push_back({{"duty", binding.duty},
			                 {"officer", officer ? officer->name : ""},
			                 {"power", label},
			                 {"controller", controller.definition.id},
			                 {"enabled_channel", controller.definition.commands[binding.enabled].id},
			                 {"target_channel", controller.definition.commands[binding.target].id},
			                 {"enabled", controller.commands[binding.enabled].value != 0},
			                 {"target", controller.commands[binding.target].value},
			                 {"work", work},
			                 {"ready", execution ? ability_recovery_until(address.ship, *execution) : 0},
			                 {"blocker", !officer                                     ? "No healthy psychic at duty"
			                             : !execution || execution->phase != "active" ? "Inactive or recovering"
			                             : work == 0 ? "Apparatus unavailable or unpowered"
			                                         : ""}});
		}
		if (!seats.empty())
			out["choirs"].push_back({{"id", id}, {"seats", seats}, {"emission", emission(address.ship)}});
	}
	for (const auto &[id, g] : ghost_receptions.records())
		if (g.observer == address.ship && g.ended >= 0)
		{
			bool permitted = false;
			for (const auto &sensor : seat(address.ship, address.console)->feed_systems)
				if (definition(address.ship).sensors.contains(sensor) &&
				    instance(entities.at(address.ship), sensor)->entity.get<SensorRuntime>().tracks.contains(g.id))
					permitted = true;
			if (!permitted)
				continue;
			const auto &p = definition(g.source).psychic.power(g.power);
			const auto age = static_cast<std::int64_t>(ecs.get<Clock>().tick) - g.ended;
			if (age < p.fade_ticks)
				out["fading"].push_back(
				    {{"body", projection::body_json(ghost_body(g))}, {"opacity", 1000 - age * 1000 / p.fade_ticks}});
		}
	return out;
}

auto Runtime::save_psychic() const -> Json
{
	Json out = {{"version", 2}, {"choirs", Json::array()}, {"receptions", Json::array()}, {"emissions", Json::array()}};
	for (auto child : ordered_equipment(choir_query))
		if (child.has<PsychicChoirState>())
			out["choirs"].push_back({{"ship", child.parent().get<StableId>().id},
			                         {"system", child.get<Equipment>().id},
			                         {"state", child.get<PsychicChoirState>()}});
	for (const auto &[id, g] : ghost_receptions.records())
		out["receptions"].push_back(g);
	out["emissions"] = weapon_emissions.save();
	return out;
}

auto Runtime::restore_psychic(const Json &saved, const std::set<EntityId> &departed_ships) -> void
{
	if (saved.empty())
	{
		for (const auto &[ship, entity] : entities)
			if (!departed_ships.contains(ship) && !definition(ship).psychic.choirs.empty())
				throw std::runtime_error("Missing saved psychic state");
		return;
	}
	if (!saved.is_object() || saved.value("version", 0) != 2 || !saved.at("choirs").is_array() ||
	    !saved.at("receptions").is_array() || !saved.at("emissions").is_array() || saved.at("choirs").size() > 2048 ||
	    saved.at("receptions").size() > 4096 || saved.at("emissions").size() > entities.size())
		throw std::runtime_error("Invalid psychic continuation; start a new session");
	std::set<std::pair<EntityId, std::string>> seen;
	for (const auto &row : saved.at("choirs"))
	{
		const EntityId ship = row.at("ship");
		const std::string id = row.at("system");
		if (!entities.contains(ship) || !definition(ship).psychic.choirs.contains(id) || !seen.emplace(ship, id).second)
			throw std::runtime_error("Invalid saved choir");
		auto state = restore_psychic_contributions(
		    row.at("state"), ship, id, entities.at(ship).try_get<AbilityRuntime>(), definition(ship).psychic);
		instance(entities.at(ship), id)->entity.set<PsychicChoirState>(std::move(state));
	}
	for (const auto &[ship, entity] : entities)
		for (const auto &[id, choir] : definition(ship).psychic.choirs)
			// Retired equipment is omitted by the active-equipment save query.
			if (!departed_ships.contains(ship) && !seen.contains({ship, id}))
				throw std::runtime_error("Missing saved choir");
	refresh_psychic_work();
	refresh_psychic_reception();
	std::set<std::tuple<EntityId, EntityId, std::string, std::string, std::uint64_t, std::size_t>> reception_keys;
	for (const auto &row : saved.at("receptions"))
	{
		auto reception = row.get<GhostReception>();
		const auto *runtime =
		    entities.contains(reception.source) ? entities.at(reception.source).try_get<AbilityRuntime>() : nullptr;
		if (Json(reception) != row || !reception.id || reception.id >= next_entity || entities.contains(reception.id) ||
		    !entities.contains(reception.observer) || !entities.contains(reception.source) ||
		    !definition(reception.source).psychic.choirs.contains(reception.choir) || reception.grant.empty() ||
		    reception.grant.size() > 2048 || !definition(reception.source).psychic.has_power(reception.power) ||
		    definition(reception.source).psychic.power(reception.power).kind != "scramble" ||
		    reception.member >=
		        static_cast<std::size_t>(definition(reception.source).psychic.power(reception.power).ghosts) ||
		    !runtime || !reception.activation || reception.activation >= runtime->next_activation ||
		    !valid_psychic_shape(reception.last) ||
		    !reception_keys
		         .emplace(reception.observer, reception.source, reception.choir, reception.grant, reception.activation,
		                  reception.member)
		         .second ||
		    reception.seen > ecs.get<Clock>().tick || reception.ended < -1 ||
		    reception.ended > static_cast<std::int64_t>(ecs.get<Clock>().tick) ||
		    (reception.ended >= 0 && reception.ended < static_cast<std::int64_t>(reception.seen)) ||
		    (reception.dispelled && reception.ended < 0) || !ghost_receptions.restore(reception))
			throw std::runtime_error("Invalid saved ghost reception");
	}
	for (const auto &[id, reception] : ghost_receptions.records())
		if (reception.ended < 0)
		{
			const auto &contributions =
			    instance(entities.at(reception.source), reception.choir)->entity.get<PsychicChoirState>().contributions;
			const auto found = contributions.find(reception.grant);
			if (found == contributions.end() || !found->second.work ||
			    found->second.activation != reception.activation || found->second.ability != reception.power ||
			    reception.member >= found->second.shapes.size() || reception.seen != ecs.get<Clock>().tick ||
			    Json(reception.last) != Json(found->second.shapes[reception.member]) ||
			    strongest_scrambler(reception.observer) !=
			        std::tuple{reception.source, reception.choir, reception.grant})
				throw std::runtime_error("Unearned live ghost reception");
		}
	weapon_emissions.restore(saved.at("emissions"), ecs.get<Clock>().tick,
	                         [this](EntityId ship)
	                         {
		                         return entities.contains(ship);
	                         });
	refresh_concealment();
}
} // namespace phoenix::world_detail
