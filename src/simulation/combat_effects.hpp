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
void damage_faults(flecs::entity ship, std::size_t compartment, std::int64_t damage)
{
	auto &s = ship.get_mut<CombatStructure>();
	for (const auto &[equipment, definitions] : s.definition.faults)
	{
		if (s.definition.compartment_ids.at(s.definition.equipment_compartments.at(equipment)) != compartment)
			continue;
		auto &states = s.faults.at(equipment);
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
			++states[i].generation;
		}
	}
}
bool combat_paid(flecs::entity ship, flecs::entity child) const
{
	if (const auto *power = ship.try_get<ShipResources>())
	{
		const auto &g = power->grants.at(child.get<ResourceBinding>().slot);
		return !g.blocked && g.supply_permille == 1000;
	}
	return false;
}
std::int64_t shield_rate(flecs::entity child) const
{
	const auto &m = child.get<CombatMechanism>();
	const auto &d = m.definition;
	return std::min((d.capacity - m.strength) * rate,
	                child.get<Equipment>().enabled ? d.recharge_rate : d.off_recharge_rate);
}
void request_shields(flecs::entity ship)
{
	auto *power = ship.try_get_mut<ShipResources>();
	if (!power)
		return;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Shield)
		{
			const auto &d = m->definition;
			const bool working = !duel_ended() && !structurally_disabled(ship, d.id);
			const auto energy = working ? (child.get<Equipment>().enabled ? d.sustain_energy : 0) +
			                                  (m->discharge ? 0 : shield_rate(child) * d.recharge_energy)
			                            : 0;
			request_system_resource(*power, child.get<ResourceBinding>().slot, 0, false, energy);
		}
}
void actuate_shields(flecs::entity ship)
{
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Shield)
		{
			const auto &d = m->definition;
			const bool enabled = child.get<Equipment>().enabled;
			const auto *power_source = ship.try_get<ShipResources>();
			const auto *grant = power_source ? &power_source->grants.at(child.get<ResourceBinding>().slot) : nullptr;
			const auto sustain = enabled ? d.sustain_energy : 0;
			const auto requested = sustain + (m->discharge ? 0 : shield_rate(child) * d.recharge_energy);
			const auto funded = grant && !grant->blocked ? requested * grant->supply_permille / 1000 : 0;
			const bool paid = grant && !grant->blocked && !structurally_disabled(ship, d.id) && funded >= sustain;
			m->warmup = paid && enabled ? std::min(d.warmup_ticks, m->warmup + 1) : 0;
			if (paid && !m->discharge)
			{
				const auto recharge = std::min(shield_rate(child), (funded - sustain) / d.recharge_energy);
				m->recharge_remainder += recharge * fault_effectiveness(ship, d.id);
				m->strength = std::min(d.capacity, m->strength + m->recharge_remainder / (rate * 1000));
				m->recharge_remainder %= rate * 1000;
			}
			if (m->discharge && paid)
				if (auto *power = ship.try_get_mut<ShipResources>())
				{
					m->discharge_remainder += d.discharge_rate;
					const auto units = std::min(m->strength, m->discharge_remainder / rate);
					m->discharge_remainder %= rate;
					const auto offered = units * d.recharge_energy * d.discharge_efficiency / 1000;
					const auto slot = power->definition.systems.at(child.get<ResourceBinding>().slot).store;
					const auto received = power->network.receive_external(slot, offered);
					const auto spent = (received * 1000 + d.recharge_energy * d.discharge_efficiency - 1) /
					                   (d.recharge_energy * d.discharge_efficiency);
					m->strength -= spent;
				}
		}
}
std::int64_t absorb_shields(flecs::entity ship, std::int64_t heading, std::int64_t damage, std::int64_t bypass,
                            std::int64_t *carry = nullptr)
{
	const auto budget = damage * bypass + (carry ? *carry : 0);
	const auto bypassed = budget / 1000;
	if (carry)
		*carry = budget % 1000;
	auto remaining = damage - bypassed;
	std::vector<flecs::entity> layers;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Shield &&
		                                                      m->warmup &&
		                                                      !structurally_disabled(ship, m->definition.id))
		{
			const auto facing = combat_angle(heading + 180000 - ship.get<Pose>().heading);
			if (std::abs(combat_delta(facing, m->definition.bearing)) <= m->definition.arc / 2)
				layers.push_back(child);
		}
	std::sort(layers.begin(), layers.end(),
	          [](auto a, auto b)
	          {
		          const auto &x = a.template get<CombatMechanism>().definition;
		          const auto &y = b.template get<CombatMechanism>().definition;
		          return std::pair{x.layer, x.id} < std::pair{y.layer, y.id};
	          });
	for (auto child : layers)
	{
		auto &m = child.get_mut<CombatMechanism>();
		const auto &d = m.definition;
		const auto available = m.strength * m.warmup / d.warmup_ticks * fault_effectiveness(ship, d.id) / 1000;
		const auto stopped = std::min(remaining, available);
		if (stopped && blast_visuals.size() < 8192)
		{
			DestructionDefinition flash;
			const auto &shape = ship.get<Collider>();
			flash.size = 2 * (shape.box ? std::max(shape.half_width, shape.half_length) : shape.radius);
			flash.growth_ticks = 1;
			flash.fade_ticks = 5;
			blast_visuals.push_back({next_blast++, ship.get<Pose>().position, 0,
			                         static_cast<std::int64_t>(ecs.get<Clock>().tick), flash, 1,
			                         combat_angle(ship.get<Pose>().heading + d.bearing), d.arc});
		}
		m.strength -= stopped;
		remaining -= stopped;
		if (!remaining)
			break;
	}
	return bypassed + remaining;
}
