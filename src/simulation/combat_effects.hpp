bool equipment_destroyed(flecs::entity ship, const std::string &equipment) const
{
	const auto *s = ship.try_get<CombatStructure>();
	if (!s || !s->faults.contains(equipment))
		return false;
	const auto &states = s->faults.at(equipment);
	const auto &defs = s->definition.faults.at(equipment);
	for (std::size_t i = 0; i < states.size(); ++i)
		if (states[i].active && defs[i].destroys)
			return true;
	return false;
}
bool hard_fault_disabled(flecs::entity ship, const std::string &equipment) const
{
	const auto *structure = ship.try_get<CombatStructure>();
	if (!structure || !structure->faults.contains(equipment))
		return false;
	const auto &states = structure->faults.at(equipment);
	const auto &definitions = structure->definition.faults.at(equipment);
	for (std::size_t i = 0; i < states.size(); ++i)
		if (states[i].active &&
		    (definitions[i].destroys || definitions[i].effect == "disabled" || definitions[i].effect == "feed"))
			return true;
	return false;
}
// Typed effects shared by weapons, physical mechanisms and repair work.
std::int64_t fault_effectiveness(flecs::entity ship, const std::string &equipment) const
{
	const auto *s = ship.try_get<CombatStructure>();
	if (!s)
		return 1000;
	const auto at = s->faults.find(equipment);
	if (at == s->faults.end())
		return 1000;
	std::int64_t result = 1000;
	const auto &definitions = s->definition.faults.at(equipment);
	for (std::size_t i = 0; i < at->second.size(); ++i)
		if (at->second[i].active)
		{
			const auto &d = definitions[i];
			result = std::min(result, d.destroys || d.effect == "disabled" || d.effect == "feed" ? std::int64_t{0}
			                                                                                     : d.effectiveness);
		}
	return result;
}
void damage_faults(flecs::entity ship, std::size_t compartment, std::int64_t damage,
                   std::vector<std::string> *affected = nullptr, const std::string &only_equipment = {})
{
	auto &s = ship.get_mut<CombatStructure>();
	for (const auto &[equipment, definitions] : s.definition.faults)
	{
		if (!only_equipment.empty() && equipment != only_equipment)
			continue;
		if (s.definition.compartment_ids.at(s.definition.equipment_compartments.at(equipment)) != compartment)
			continue;
		auto &states = s.faults.at(equipment);
		bool struck = false;
		for (std::size_t i = 0; i < definitions.size(); ++i)
		{
			const auto &d = definitions[i];
			if (damage < d.threshold)
				continue;
			s.random = s.random * 6364136223846793005ULL + 1;
			if ((s.random >> 16) % 1000 >= static_cast<std::uint64_t>(d.chance))
				continue;
			states[i].active = true;
			states[i].diagnosed = false;
			states[i].work = 0;
			struck = true;
		}
		if (struck && affected)
			affected->push_back(equipment);
	}
}
const ShieldContributionDefinition &shield_contribution(const ShieldEffectDefinition &definition,
                                                        const std::string &name) const
{
	const auto entry = std::find_if(definition.contributions.begin(), definition.contributions.end(),
	                                [&](const auto &d)
	                                {
		                                return d.name == name;
	                                });
	if (entry == definition.contributions.end())
		throw std::runtime_error("Unknown shield contribution");
	return *entry;
}
std::uint64_t install_shield_grant(ShieldComponent &owner, const ShieldEffectDefinition &definition,
                                   const std::string &source, const std::string &equipment,
                                   const std::string &definition_id, std::int64_t expiry = -1)
{
	validate_effect_identity(source, equipment, false);
	if (expiry < -1 || expiry > 9007199254740991LL)
		throw std::runtime_error("Effect expiry outside supported bounds");
	const auto count = definition.contributions.size();
	if (!count)
		throw std::runtime_error("Shield grant has no compiled contributions");
	if (owner.contributions.size() + count > 8192 || owner.next_effect >= 9007199254740991ULL)
		throw std::runtime_error("Shield grant capacity exhausted");
	const auto id = owner.next_effect;
	std::vector<ShieldContribution> prepared;
	prepared.reserve(count);
	for (const auto &entry : definition.contributions)
	{
		ShieldContribution c;
		c.effect_id = id;
		c.source = source;
		c.equipment = equipment;
		c.definition_id = definition_id;
		c.definition = entry;
		c.state.strength = entry.shield.reserve.initial;
		c.expiry = expiry;
		prepared.push_back(std::move(c));
	}
	owner.contributions.reserve(owner.contributions.size() + prepared.size());
	for (auto &c : prepared)
		owner.contributions.push_back(std::move(c));
	++owner.next_effect;
	return id;
}
std::int64_t shield_property(flecs::entity ship, const ShieldContribution &c, const std::string &property,
                             std::int64_t base, std::int64_t extra_delta = 0) const
{
	return effective_property(ship, c.equipment, property, base, extra_delta,
	                          "effect:" + std::to_string(c.effect_id) + ":" + c.definition.name);
}
void step_shield_effects()
{
	for (const auto &[id, ship] : combat_rows())
		if (auto *owner = ship.try_get_mut<ShieldComponent>())
			std::erase_if(owner->contributions,
			              [&](const auto &c)
			              {
				              return c.expiry >= 0 && static_cast<std::int64_t>(ecs.get<Clock>().tick) >= c.expiry;
			              });
}
void request_shields(flecs::entity ship)
{
	auto *owner = ship.try_get_mut<ShieldComponent>();
	auto *power = ship.try_get_mut<ShipResources>();
	if (!owner || !power)
		return;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Shield)
		{
			std::int64_t energy = 0;
			for (auto &c : owner->contributions)
				if (c.equipment == m->definition.id)
				{
					const auto &d = c.definition.shield;
					c.operating = !duel_ended() && !structurally_disabled(ship, c.equipment);
					const auto base = child.get<Equipment>().enabled ? d.recharge_rate : d.off_recharge_rate;
					c.quoted_rate =
					    std::min((d.reserve.capacity - c.state.strength) * rate * 1000,
					             shield_property(ship, c, child.get<Equipment>().enabled ? "recharge" : "off_recharge",
					                             base * 1000, d.recharge_modifier));
					c.protection = std::clamp<std::int64_t>(
					    shield_property(ship, c, "protection", 1000, d.protection_modifier), 0, 1000);
					c.quoted_sustain = shield_property(ship, c, "sustain", d.sustain_energy, d.sustain_modifier);
					c.quoted_discharge = shield_property(ship, c, "discharge", d.discharge_rate, d.discharge_modifier);
					if (c.operating)
						energy += (child.get<Equipment>().enabled ? c.quoted_sustain : 0) +
						          (c.state.discharge ? 0 : (c.quoted_rate * d.recharge_energy + 999) / 1000);
				}
			request_system_resource(*power, child.get<ResourceBinding>().slot, 0, false, energy);
		}
}
void actuate_shields(flecs::entity ship)
{
	auto *owner = ship.try_get_mut<ShieldComponent>();
	if (!owner)
		return;
	for (auto &c : owner->contributions)
		if (c.equipment.empty())
		{
			const auto &d = c.definition.shield;
			c.quoted_rate = shield_property(ship, c, "recharge", d.recharge_rate * 1000, d.recharge_modifier);
			c.quoted_sustain = 0;   // Independent scenario grants explicitly use the cost-free process.
			c.quoted_discharge = 0; // No authored discharge destination exists for this process.
			c.protection =
			    std::clamp<std::int64_t>(shield_property(ship, c, "protection", 1000, d.protection_modifier), 0, 1000);
		}
	std::map<std::string, std::int64_t> budgets, sustains;
	std::map<std::string, bool> paid_equipment;
	for (const auto &c : owner->contributions)
		if (!c.equipment.empty())
		{
			const auto child = instance(ship, c.equipment)->entity;
			const auto &d = c.definition.shield;
			if (c.operating)
			{
				budgets[c.equipment] += (child.get<Equipment>().enabled ? c.quoted_sustain : 0) +
				                        (c.state.discharge ? 0 : (c.quoted_rate * d.recharge_energy + 999) / 1000);
				sustains[c.equipment] += child.get<Equipment>().enabled ? c.quoted_sustain : 0;
			}
		}
	for (auto &[equipment, budget] : budgets)
	{
		const auto *power = ship.try_get<ShipResources>();
		const auto child = instance(ship, equipment)->entity;
		const auto *grant = power ? &power->grants.at(child.get<ResourceBinding>().slot) : nullptr;
		budget = grant && !grant->blocked ? budget * grant->supply_permille / 1000 : 0;
		paid_equipment[equipment] = grant && !grant->blocked && budget >= sustains[equipment];
		budget = std::max<std::int64_t>(0, budget - sustains[equipment]);
	}
	for (auto &c : owner->contributions)
	{
		auto &s = c.state;
		const auto &d = c.definition.shield;
		const bool independent = c.equipment.empty();
		const bool enabled = independent || instance(ship, c.equipment)->entity.get<Equipment>().enabled;
		const bool paid = c.operating && (independent || paid_equipment[c.equipment]);
		s.warmup = paid && enabled ? std::min(d.warmup_ticks, s.warmup + 1) : 0;
		if (paid && !s.discharge)
		{
			const auto desired =
			    independent ? std::min((d.reserve.capacity - s.strength) * rate * 1000, c.quoted_rate) : c.quoted_rate;
			auto recharge = desired;
			if (!independent)
			{
				recharge = std::min(desired, budgets[c.equipment] * 1000 / d.recharge_energy);
				budgets[c.equipment] -= (recharge * d.recharge_energy + 999) / 1000;
			}
			s.recharge_remainder += recharge;
			s.strength = std::min(d.reserve.capacity, s.strength + s.recharge_remainder / (rate * 1000));
			s.recharge_remainder %= rate * 1000;
		}
		if (s.discharge && paid && !independent)
			if (auto *power = ship.try_get_mut<ShipResources>())
			{
				const auto child = instance(ship, c.equipment)->entity;
				s.discharge_remainder += c.quoted_discharge;
				const auto units = std::min(s.strength, s.discharge_remainder / rate);
				const auto conversion = d.recharge_energy * d.discharge_efficiency;
				const auto offered = (s.discharge_credit + units * conversion) / 1000;
				const auto slot = power->definition.systems.at(child.get<ResourceBinding>().slot).store;
				const auto received = power->network.receive_external(slot, offered);
				const auto spent =
				    std::max<std::int64_t>(0, (received * 1000 - s.discharge_credit + conversion - 1) / conversion);
				s.discharge_credit += spent * conversion - received * 1000;
				s.strength -= spent;
				if (offered > 0)
					s.discharge_remainder %= rate;
				else
					s.discharge_remainder =
					    std::min(s.discharge_remainder, rate * ((1000 + conversion - 1) / conversion));
			}
	}
}
bool shield_available(flecs::entity ship, const ShieldContribution &c) const
{
	return c.operating && (c.equipment.empty() || !structurally_disabled(ship, c.equipment));
}
std::int64_t absorb_shields(flecs::entity ship, std::int64_t heading, std::int64_t damage, std::int64_t bypass,
                            std::int64_t *carry = nullptr, EntityId source = 0, const Projectile *payload = nullptr)
{
	ensure_shield_reaction_capacity(ship);
	const auto hit = next_shield_hit++;
	const auto budget = damage * bypass + (carry ? *carry : 0);
	const auto bypassed = budget / 1000;
	if (carry)
		*carry = budget % 1000;
	auto remaining = damage - bypassed;
	auto *owner = ship.try_get_mut<ShieldComponent>();
	if (!owner)
		return damage;
	std::vector<ShieldContribution *> layers;
	for (auto &c : owner->contributions)
		if (shield_available(ship, c) && c.state.warmup && c.state.strength &&
		    std::abs(combat_delta(combat_angle(heading + 180000 - ship.get<Pose>().heading), c.definition.bearing)) <=
		        c.definition.arc / 2)
			layers.push_back(&c);
	std::sort(layers.begin(), layers.end(),
	          [](auto a, auto b)
	          {
		          return std::tuple{a->definition.shield.layer, a->effect_id, a->definition.name} <
		                 std::tuple{b->definition.shield.layer, b->effect_id, b->definition.name};
	          });
	for (std::size_t begin = 0; begin < layers.size() && remaining;)
	{
		auto end = begin + 1;
		while (end < layers.size() && layers[end]->definition.shield.layer == layers[begin]->definition.shield.layer)
			++end;
		std::vector<std::int64_t> capacity(end - begin), absorbed(end - begin);
		for (std::size_t i = begin; i < end; ++i)
		{
			auto &c = *layers[i];
			c.protection = std::clamp<std::int64_t>(
			    shield_property(ship, c, "protection", 1000, c.definition.shield.protection_modifier), 0, 1000);
			const auto &d = c.definition.shield;
			const auto usable =
			    std::min(c.state.strength, c.state.strength * c.state.warmup / d.warmup_ticks * c.protection / 1000);
			// Allocate incoming damage, then transform it once. Cancellation can remove an entire share even from a weak reserve.
			capacity[i - begin] = d.pre_damage_permille == 0
			                          ? remaining
			                          : std::min(remaining, ((usable + 1) * 1000 - 1) / d.pre_damage_permille);
		}
		// Water-fill equal shares; depleted peers relinquish unused shares before the next layer.
		for (;;)
		{
			std::int64_t count = 0;
			for (std::size_t i = 0; i < capacity.size(); ++i)
				if (capacity[i] > absorbed[i])
					++count;
			if (!count || !remaining)
				break;
			const auto share = remaining / count, residue = remaining % count;
			std::int64_t rank = 0, used = 0;
			for (std::size_t i = 0; i < capacity.size(); ++i)
				if (capacity[i] > absorbed[i])
				{
					const auto amount = std::min(capacity[i] - absorbed[i], share + (rank++ < residue ? 1 : 0));
					absorbed[i] += amount;
					used += amount;
				}
			remaining -= used;
			if (!used)
				break;
		}
		for (std::size_t i = begin; i < end; ++i)
		{
			auto &c = *layers[i];
			const auto assigned = absorbed[i - begin];
			const auto &definition = c.definition.shield;
			const auto stopped = assigned * definition.pre_damage_permille / 1000;
			c.state.strength -= stopped;
			if (assigned)
				queue_shield_reactions(ship, c, hit, source, assigned, stopped, assigned - stopped, stopped, remaining);
			if (payload)
				queue_payload_outcome(
				    *payload,
				    {ship.get<StableId>().id, PayloadOutcomeKind::Shield, -1, {}, c.effect_id, c.definition.name},
				    stopped);
			if (stopped && blast_visuals.size() < 8192)
			{
				DestructionDefinition flash;
				const auto &shape = ship.get<Collider>();
				flash.size = 2 * (shape.box ? std::max(shape.half_width, shape.half_length) : shape.radius);
				flash.growth_ticks = 1;
				flash.fade_ticks = 5;
				blast_visuals.push_back(
				    {next_blast++, ship.get<Pose>().position, 0, static_cast<std::int64_t>(ecs.get<Clock>().tick),
				     flash, 1, combat_angle(ship.get<Pose>().heading + c.definition.bearing), c.definition.arc});
			}
		}
		begin = end;
	}
	return bypassed + remaining;
}
