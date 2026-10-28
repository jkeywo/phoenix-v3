// World adapter. Apparatus components own commitments; receiver records own apparent evidence.
PsychicReceptions ghost_receptions;
std::map<EntityId, std::int64_t> concealment_cache;
std::map<EntityId, std::uint64_t> weapon_emissions;
void psychic_weapon_emission(EntityId ship)
{
	weapon_emissions[ship] = ecs.get<Clock>().tick + 20;
}
std::int64_t emission(EntityId ship) const
{
	std::int64_t result = 0;
	const auto entity = entities.at(ship);
	for (auto child : entity.get<EquipmentIndex>().values)
	{
		const auto &e = child.get<Equipment>();
		if (!e.enabled || structurally_disabled(entity, e.id))
			continue;
		std::int64_t funded = 1000;
		if (const auto *p = entity.try_get<ShipResources>(); p && child.has<ResourceBinding>())
			funded = p->grants.at(child.get<ResourceBinding>().slot).supply_permille;
		if (e.kind == OperatingSystem::Generator)
			if (const auto *power = entity.try_get<ShipResources>(); power && child.has<ResourceBinding>())
			{
				const auto generator = power->definition.systems[child.get<ResourceBinding>().slot].generator;
				if (generator != no_resource)
					result += std::min<std::int64_t>(
					    200,
					    power->network.generators()[generator].actual_rate * 200 /
					        std::max<std::int64_t>(1, power->definition.network.generators[generator].maximum_rate));
			}
		if (child.has<Engine>())
			result += std::abs(child.get<Engine>().forward_output) * funded / 2000;
		if (child.has<Radar>() && child.get<Radar>().enabled && directed_target(ship, e.id))
			result += funded / 4;
	}
	if (weapon_emissions.contains(ship) && weapon_emissions.at(ship) > ecs.get<Clock>().tick)
		result += 500;
	for (const auto &[id, c] : conversations.conversations)
		for (const auto &m : c.messages)
			if (m.sender == "ship:" + std::to_string(ship) && m.sent + 20 >= ecs.get<Clock>().tick)
				result += 250;
	return std::min<std::int64_t>(1000, result);
}
const OfficerDefinition *choir_officer(EntityId ship, const std::string &duty) const
{
	const auto &p = definition(ship).personnel;
	const auto &s = entities.at(ship).get<PersonnelState>();
	for (std::size_t i = 0; i < s.officers.size(); ++i)
		if (s.officers[i].duty == duty && s.officers[i].present && officer_healthy(s.officers[i]))
			return &p.officers[i];
	return nullptr;
}
bool choir_assignment(EntityId ship, const OfficerDefinition &officer, const PersonnelDuty *duty) const
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
const AbilityGrantState *psychic_execution(EntityId ship, const PsychicContribution &contribution) const
{
	const auto *execution = ability_execution(ship, contribution.grant);
	return execution && execution->definition == contribution.ability &&
	               execution->activation == contribution.activation
	           ? execution
	           : nullptr;
}
const PsychicPowerDefinition &contribution_power(EntityId ship, const PsychicContribution &contribution) const
{
	return definition(ship).psychic.power(contribution.ability);
}
void refresh_psychic_work()
{
	for (auto child : ordered_equipment(choir_query))
	{
		const auto ship = child.parent().get<StableId>().id;
		for (auto &[grant, contribution] : child.get_mut<PsychicChoirState>().contributions)
		{
			const auto *execution = psychic_execution(ship, contribution);
			contribution.work = execution && execution->phase == "active" && !departed(ship)
			                        ? contribution_power(ship, contribution).strength * execution->funding_permille /
			                              1000 * execution->fault_permille / 1000
			                        : 0;
			if (execution)
			{
				contribution.aim_x = execution->aim_x;
				contribution.aim_y = execution->aim_y;
			}
		}
	}
}
// Native state follows committed ability executions. A funded pause keeps the same
// activation/shapes; only a new execution can create a new projection identity.
void sync_psychic_contributions()
{
	for (auto child : ordered_equipment(choir_query))
	{
		const auto ship = child.parent().get<StableId>().id;
		const auto &apparatus = child.get<Equipment>().id;
		auto &contributions = child.get_mut<PsychicChoirState>().contributions;
		std::erase_if(contributions,
		              [&](const auto &entry)
		              {
			              const auto *execution = psychic_execution(ship, entry.second);
			              return !execution || execution->apparatus != apparatus ||
			                     (execution->phase != "active" &&
			                      !(execution->phase == "paused" && execution->resume_phase == "active"));
		              });
		const auto *runtime = entities.at(ship).try_get<AbilityRuntime>();
		if (!runtime)
			continue;
		for (const auto &[grant, execution] : runtime->grants)
		{
			if (execution.apparatus != apparatus || execution.phase != "active" ||
			    !definition(ship).psychic.has_power(execution.definition))
				continue;
			auto found = contributions.find(grant);
			if (found != contributions.end() && found->second.activation == execution.activation)
				continue;
			PsychicContribution contribution;
			contribution.grant = grant;
			contribution.ability = execution.definition;
			contribution.activation = execution.activation;
			contribution.aim_x = execution.aim_x;
			contribution.aim_y = execution.aim_y;
			const auto &profile = contribution_power(ship, contribution);
			if (profile.kind == "scramble" && execution.target != ship && entities.contains(execution.target))
			{
				const auto &pose = entities.at(execution.target).get<Pose>();
				for (std::int64_t g = 0; g < profile.ghosts; ++g)
				{
					PsychicGhostShape shape;
					shape.pose.position.x = contribution.aim_x + profile.offset;
					shape.pose.position.y = contribution.aim_y + (g - (profile.ghosts - 1) / 2) * profile.spacing;
					shape.pose.heading = pose.heading;
					shape.pose.altitude = pose.altitude;
					contribution.shapes.push_back(shape);
				}
			}
			contributions.insert_or_assign(grant, std::move(contribution));
		}
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
bool psychic_coverage(EntityId ship, const ChoirDefinition &c, const PsychicContribution &s, EntityId receiver) const
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
std::int64_t compute_concealment(EntityId target) const
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
void refresh_concealment()
{
	concealment_cache.clear();
	for (const auto &[ship, entity] : entities)
		if (entity.has<EquipmentIndex>())
			concealment_cache[ship] = compute_concealment(ship);
}
std::int64_t concealment(EntityId target) const
{
	const auto value = concealment_cache.find(target);
	return value == concealment_cache.end() ? 0 : value->second;
}
void dispel_ghost(const GhostReception &g)
{
	if (ghost_receptions.dispel(g.id, ecs.get<Clock>().tick))
		record_sensor_dispel(g);
}
bool ghost_live(const GhostReception &g) const
{
	return psychic_ghost_live(g, ecs.get<Clock>().tick);
}
using PsychicSource = std::tuple<EntityId, std::string, std::string>;
std::map<EntityId, PsychicSource> scrambler_winners;
void refresh_psychic_reception()
{
	scrambler_winners.clear();
	const auto choirs = ordered_equipment(choir_query);
	for (const auto &[receiver, entity] : entities)
	{
		if (!entity.has<EquipmentIndex>())
			continue;
		std::int64_t strength = 0;
		PsychicSource winner{};
		for (bool wards : {true, false})
			for (auto child : choirs)
			{
				const auto ship = child.parent().get<StableId>().id;
				const auto &c = definition(ship).psychic.choirs.at(child.get<Equipment>().id);
				const auto &contributions = child.get<PsychicChoirState>().contributions;
				for (const auto &[grant, s] : contributions)
				{
					const auto *execution = psychic_execution(ship, s);
					if (!execution)
						continue;
					if (s.work <= strength)
						continue;
					const bool ward = contribution_power(ship, s).kind == "ward";
					if (ward != wards ||
					    (ward ? execution->target != receiver : ship == receiver || s.shapes.empty()) ||
					    !psychic_coverage(ship, c, s, receiver))
						continue;
					strength = s.work;
					if (!ward)
						winner = {ship, c.id, grant};
				}
			}
		scrambler_winners.emplace(receiver, winner);
	}
}
PsychicSource strongest_scrambler(EntityId receiver) const
{
	const auto found = scrambler_winners.find(receiver);
	return found == scrambler_winners.end() ? PsychicSource{} : found->second;
}
void advance_psychic()
{
	refresh_psychic_work();
	refresh_psychic_reception();
	refresh_concealment();
	const auto tick = ecs.get<Clock>().tick;
	std::erase_if(weapon_emissions,
	              [&](const auto &v)
	              {
		              return !entities.contains(v.first) || v.second <= tick;
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
BodyView ghost_body(const GhostReception &g) const
{
	return psychic_ghost_body(g, definition(g.source).psychic.power(g.power).apparent_class);
}
void ghost_damage(EntityId observer, fixed::Vec from, fixed::Vec to, std::int64_t from_z, std::int64_t to_z,
                  std::int64_t radius = 0)
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
Json psychic_board(const ConsoleAddress &address) const
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
Json save_psychic() const
{
	Json out = {{"version", 2}, {"choirs", Json::array()}, {"receptions", Json::array()}, {"emissions", Json::array()}};
	for (auto child : ordered_equipment(choir_query))
		if (child.has<PsychicChoirState>())
			out["choirs"].push_back({{"ship", child.parent().get<StableId>().id},
			                         {"system", child.get<Equipment>().id},
			                         {"state", child.get<PsychicChoirState>()}});
	for (const auto &[id, g] : ghost_receptions.records())
		out["receptions"].push_back(g);
	for (const auto &[ship, until] : weapon_emissions)
		out["emissions"].push_back({{"ship", ship}, {"until", until}});
	return out;
}
void restore_psychic(const Json &saved, const std::set<EntityId> &departed_ships)
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
	const auto shape_valid = [](const PsychicGhostShape &shape)
	{
		const auto in = [](std::int64_t value, std::int64_t lo, std::int64_t hi)
		{
			return value >= lo && value <= hi;
		};
		return in(shape.pose.position.x, -1000000000, 1000000000) &&
		       in(shape.pose.position.y, -1000000000, 1000000000) && in(shape.motion.velocity.x, -1000000, 1000000) &&
		       in(shape.motion.velocity.y, -1000000, 1000000) && in(shape.pose.altitude, -1000000, 1000000) &&
		       in(shape.pose.heading, 0, 359999) && in(shape.motion.remainder.x, 1 - rate, rate - 1) &&
		       in(shape.motion.remainder.y, 1 - rate, rate - 1) && in(shape.acceleration_remainder, 0, rate - 1) &&
		       in(shape.turn_remainder, 0, rate - 1);
	};
	std::set<std::pair<EntityId, std::string>> seen;
	for (const auto &row : saved.at("choirs"))
	{
		const EntityId ship = row.at("ship");
		const std::string id = row.at("system");
		if (!entities.contains(ship) || !definition(ship).psychic.choirs.contains(id) || !seen.emplace(ship, id).second)
			throw std::runtime_error("Invalid saved choir");
		auto state = row.at("state").get<PsychicChoirState>();
		if (Json(state) != row.at("state") || state.contributions.size() > 256)
			throw std::runtime_error("Invalid saved psychic contributions");
		for (const auto &[grant, contribution] : state.contributions)
		{
			const auto *execution = psychic_execution(ship, contribution);
			if (grant != contribution.grant || !execution || execution->apparatus != id ||
			    !definition(ship).psychic.has_power(contribution.ability) || !contribution.activation ||
			    (execution->phase != "active" &&
			     !(execution->phase == "paused" && execution->resume_phase == "active")) ||
			    contribution.aim_x != execution->aim_x || contribution.aim_y != execution->aim_y)
				throw std::runtime_error("Invalid psychic execution link");
			const auto &profile = contribution_power(ship, contribution);
			if ((profile.kind == "scramble" &&
			     (contribution.shapes.size() != static_cast<std::size_t>(profile.ghosts) || !execution->target ||
			      execution->target == ship)) ||
			    (profile.kind != "scramble" && !contribution.shapes.empty()))
				throw std::runtime_error("Contradictory psychic projection");
			for (const auto &shape : contribution.shapes)
				if (!shape_valid(shape))
					throw std::runtime_error("Invalid apparent trajectory");
		}
		if (const auto *runtime = entities.at(ship).try_get<AbilityRuntime>())
			for (const auto &[grant, execution] : runtime->grants)
				if (execution.apparatus == id && definition(ship).psychic.has_power(execution.definition) &&
				    (execution.phase == "active" ||
				     (execution.phase == "paused" && execution.resume_phase == "active")) &&
				    !state.contributions.contains(grant))
					throw std::runtime_error("Missing saved psychic contribution");
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
		    !shape_valid(reception.last) ||
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
	for (const auto &row : saved.at("emissions"))
	{
		const EntityId ship = row.at("ship");
		const std::uint64_t until = row.at("until");
		if (!entities.contains(ship) || until > ecs.get<Clock>().tick + 20 ||
		    !weapon_emissions.emplace(ship, until).second)
			throw std::runtime_error("Invalid saved emission");
	}
	refresh_concealment();
}
