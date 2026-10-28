#include "../../effects/rules/damage_rules.hpp"
// Short-lived cosmetic receipts are deliberately absent from checkpoints.
// Playback baselines on a new world, while continuous cues come from live state.
std::map<EntityId, std::vector<SceneAudio>> audio_events;
std::uint64_t next_audio_event = 1;
void emit_audio(flecs::entity ship, SceneAudio cue)
{
	if (cue.clip.asset.empty())
		return;
	auto &events = audio_events[ship.get<StableId>().id];
	if (events.size() >= 256)
		events.erase(events.begin());
	cue.event = next_audio_event++;
	cue.tick = ecs.get<Clock>().tick;
	events.push_back(std::move(cue));
}
void retire_audio()
{
	const auto now = ecs.get<Clock>().tick;
	for (auto &[id, events] : audio_events)
		std::erase_if(events,
		              [&](const auto &cue)
		              {
			              return now > cue.tick + 3;
		              });
	std::erase_if(audio_events,
	              [](const auto &entry)
	              {
		              return entry.second.empty();
	              });
}
bool equipment_destroyed(flecs::entity ship, const std::string &equipment) const
{
	return phoenix::equipment_destroyed(ship.try_get<CombatStructure>(), equipment);
}
bool hard_fault_disabled(flecs::entity ship, const std::string &equipment) const
{
	return phoenix::hard_fault_disabled(ship.try_get<CombatStructure>(), equipment);
}
std::int64_t fault_effectiveness(flecs::entity ship, const std::string &equipment) const
{
	return phoenix::fault_effectiveness(ship.try_get<CombatStructure>(), equipment);
}
void damage_faults(flecs::entity ship, std::size_t compartment, std::int64_t damage,
                   std::vector<std::string> *affected = nullptr, const std::string &only_equipment = {},
                   const NativeRuleContext *origin = nullptr, const std::vector<NativeRuleSource> *sources = nullptr)
{
	const auto local_sources = sources ? std::vector<NativeRuleSource>{} : rule_sources(ship);
	if (!sources)
		sources = &local_sources;
	auto &s = ship.get_mut<CombatStructure>();
	for (const auto &[equipment, definitions] : s.definition.faults)
	{
		if (!only_equipment.empty() && equipment != only_equipment)
			continue;
		if (s.definition.compartment_ids.at(s.definition.equipment_compartments.at(equipment)) != compartment)
			continue;
		auto context = damage_rule_context(
		    {ship.get<StableId>().id, PayloadOutcomeKind::System, static_cast<std::int64_t>(compartment), equipment},
		    damage, 0, nullptr, origin);
		const auto system_damage =
		    damage > 0 ? apply_pre_damage_rules(ship, RuleTrigger::PreSystemDamage, context, sources) : 0;
		auto &states = s.faults.at(equipment);
		bool struck = false;
		for (std::size_t i = 0; i < definitions.size(); ++i)
		{
			const auto &d = definitions[i];
			if (system_damage <= 0 || system_damage < d.threshold)
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
		if (damage > 0)
		{
			context.inflicted = struck ? system_damage : 0;
			context.residual = system_damage - context.inflicted;
			emit_damage_rules(ship, RuleTrigger::PostSystemDamage, context, nullptr, sources);
		}
	}
}

std::int64_t shield_property(flecs::entity ship, const ShieldContribution &c, const std::string &property,
                             std::int64_t base, std::int64_t extra_delta = 0) const
{
	return effective_property(ship, c.equipment, property, base, extra_delta,
	                          "effect:" + std::to_string(c.effect_id) + ":" + c.definition.name);
}
std::int64_t shield_property(flecs::entity ship, const ShieldContribution &c, PropertyKey property, std::int64_t base,
                             std::int64_t extra_delta = 0) const
{
	return shield_property(ship, c, modifier_property(property).id, base, extra_delta);
}
void step_shield_effects()
{
	for (const auto &[id, ship] : combat_rows())
		if (auto *owner = ship.try_get_mut<ShieldComponent>())
			expire_shield_grants(*owner, static_cast<std::int64_t>(ecs.get<Clock>().tick));
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
					const auto output = rule_source_output(ship.get<StableId>().id, c.source);
					c.operating = !duel_ended() && !structurally_disabled(ship, c.equipment) && output > 0;
					const auto base = child.get<Equipment>().enabled ? d.recharge_rate : d.off_recharge_rate;
					c.quoted_rate =
					    std::min((d.reserve.capacity - c.state.strength) * rate * 1000,
					             shield_property(ship, c, child.get<Equipment>().enabled ? "recharge" : "off_recharge",
					                             base * 1000, d.recharge_modifier));
					c.protection = std::clamp<std::int64_t>(
					    shield_property(ship, c, "protection", 1000, d.protection_modifier), 0, 1000);
					c.quoted_sustain = shield_property(ship, c, "sustain", d.sustain_energy, d.sustain_modifier);
					c.quoted_discharge = shield_property(ship, c, "discharge", d.discharge_rate, d.discharge_modifier);
					c.quoted_rate = c.quoted_rate * output / 1000;
					c.quoted_sustain = c.quoted_sustain * output / 1000;
					c.quoted_discharge = c.quoted_discharge * output / 1000;
					c.protection = c.protection * output / 1000;
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
			const auto output = rule_source_output(ship.get<StableId>().id, c.source);
			c.quoted_rate = shield_property(ship, c, "recharge", d.recharge_rate * 1000, d.recharge_modifier);
			c.quoted_sustain = 0;   // Independent scenario grants explicitly use the cost-free process.
			c.quoted_discharge = 0; // No authored discharge destination exists for this process.
			c.protection =
			    std::clamp<std::int64_t>(shield_property(ship, c, "protection", 1000, d.protection_modifier), 0, 1000);
			c.quoted_rate = c.quoted_rate * output / 1000;
			c.protection = c.protection * output / 1000;
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
		// Paused maintained grants retain all reserve/warmup/remainder state.
		// Availability is gated separately until their next paid native lease.
		if (!rule_grant_available(ship.get<StableId>().id, c.source))
			continue;
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
	return rule_grant_available(ship.get<StableId>().id, c.source) && c.operating &&
	       (c.equipment.empty() || !structurally_disabled(ship, c.equipment));
}
std::int64_t absorb_shields(flecs::entity ship, std::int64_t heading, std::int64_t damage, std::int64_t bypass,
                            std::int64_t *carry = nullptr, EntityId source = 0, const Projectile *payload = nullptr,
                            const NativeRuleContext *origin = nullptr,
                            const std::vector<NativeRuleSource> *sources = nullptr,
                            std::int64_t *native_inflicted = nullptr)
{
	const auto local_sources = sources ? std::vector<NativeRuleSource>{} : rule_sources(ship);
	if (!sources)
		sources = &local_sources;
	const auto budget = damage * bypass + (carry ? *carry : 0);
	const auto bypassed = budget / 1000;
	if (carry)
		*carry = budget % 1000;
	auto remaining = damage - bypassed;
	const auto *owner = ship.try_get<ShieldComponent>();
	if (!owner)
		return damage;
	// Pre-effects may replace the component or its contribution vector. Native
	// contact geometry and reserves remain the values captured for this hit.
	std::vector<ShieldContribution> layers;
	std::map<std::pair<std::uint64_t, std::string>, NativeRuleContext> shield_origins;
	for (auto c : owner->contributions)
		if (shield_available(ship, c) && c.state.warmup && c.state.strength &&
		    std::abs(combat_delta(combat_angle(heading + 180000 - ship.get<Pose>().heading), c.definition.bearing)) <=
		        c.definition.arc / 2)
		{
			c.protection = std::clamp<std::int64_t>(
			    shield_property(ship, c, "protection", 1000, c.definition.shield.protection_modifier), 0, 1000);
			c.protection = c.protection * rule_source_output(ship.get<StableId>().id, c.source) / 1000;
			shield_origins.emplace(std::pair{c.effect_id, c.definition.name},
			                       native_rule_origin({ship.get<StableId>().id, c.equipment}, c.source,
			                                          NativeEffectKind::Shield, c.effect_id));
			layers.push_back(std::move(c));
		}
	std::sort(layers.begin(), layers.end(),
	          [](const auto &a, const auto &b)
	          {
		          return std::tuple{a.definition.shield.layer, a.effect_id, a.definition.name} <
		                 std::tuple{b.definition.shield.layer, b.effect_id, b.definition.name};
	          });
	for (std::size_t begin = 0; begin < layers.size() && remaining;)
	{
		auto end = begin + 1;
		while (end < layers.size() && layers[end].definition.shield.layer == layers[begin].definition.shield.layer)
			++end;
		const auto incoming = remaining;
		std::vector<std::vector<DamageRuleFrame>> frames(end - begin);
		for (std::size_t i = begin; i < end; ++i)
		{
			const auto &c = layers[i];
			auto context = damage_rule_context(
			    {ship.get<StableId>().id, PayloadOutcomeKind::Shield, -1, {}, c.effect_id, c.definition.name}, incoming,
			    source, payload, origin);
			context.strength = c.state.strength;
			frames[i - begin] = freeze_damage_rules(ship, RuleTrigger::PreShieldDamage, context, &c, sources,
			                                        &shield_origins.at({c.effect_id, c.definition.name}));
		}
		std::vector<std::int64_t> capacity(end - begin), absorbed(end - begin), factors(end - begin);
		for (;;)
		{
			remaining = incoming;
			std::fill(absorbed.begin(), absorbed.end(), 0);
			for (std::size_t i = begin; i < end; ++i)
			{
				const auto &c = layers[i];
				const auto &d = c.definition.shield;
				const auto usable = std::min(c.state.strength,
				                             c.state.strength * c.state.warmup / d.warmup_ticks * c.protection / 1000);
				const auto factor = factors[i - begin] = damage_rule_factor(frames[i - begin]);
				capacity[i - begin] = factor == 0 ? incoming : std::min(incoming, ((usable + 1) * 1000 - 1) / factor);
			}
			// Existing equal-share water-fill, including deterministic residue.
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
			std::vector<EffectGroupInvocation> invocations;
			std::vector<std::pair<std::size_t, std::size_t>> indices;
			for (std::size_t i = 0; i < frames.size(); ++i)
				if (absorbed[i])
					for (std::size_t j = 0; j < frames[i].size(); ++j)
						if (!frames[i][j].disabled)
						{
							indices.emplace_back(i, j);
							invocations.push_back(damage_rule_invocation(frames[i][j]));
						}
			if (invocations.empty())
				break;
			auto quote = prepare_effect_groups(invocations);
			quote.defer_damage = true;
			if (!quote.failure.empty())
			{
				if (quote.failed_invocation < indices.size())
				{
					const auto [i, j] = indices[quote.failed_invocation];
					frames[i][j].disabled = true;
				}
				else
					for (auto &row : frames)
						for (auto &frame : row)
							frame.disabled = true;
				continue;
			}
			if (!commit_effect_group(quote).empty())
			{
				for (auto &row : frames)
					for (auto &frame : row)
						frame.disabled = true;
				continue;
			}
			break;
		}
		for (std::size_t i = begin; i < end; ++i)
		{
			auto &c = layers[i];
			const auto assigned = absorbed[i - begin];
			if (!assigned)
				continue;
			const auto &definition = c.definition.shield;
			const auto stopped = assigned * factors[i - begin] / 1000;
			c.state.strength -= stopped;
			if (native_inflicted)
				*native_inflicted += stopped;
			if (auto *live = ship.try_get_mut<ShieldComponent>())
				for (auto &current : live->contributions)
					if (current.effect_id == c.effect_id && current.definition.name == c.definition.name)
						current.state.strength = c.state.strength;
			auto context = damage_rule_context(
			    {ship.get<StableId>().id, PayloadOutcomeKind::Shield, -1, {}, c.effect_id, c.definition.name}, assigned,
			    source, payload, origin);
			context.modified = stopped;
			context.negated = assigned - stopped;
			context.absorbed = stopped;
			context.residual = remaining;
			context.strength = c.state.strength;
			emit_damage_rules(ship, RuleTrigger::PostShieldDamage, context, &c, sources,
			                  &shield_origins.at({c.effect_id, c.definition.name}));
			if (payload)
				queue_payload_outcome(*payload, context.hit, stopped);
			if (assigned && !definition.audio.asset.empty())
			{
				SceneAudio cue{SceneAudioKind::Shield, "shield", definition.audio};
				cue.clip.gain = 0; // The live contribution publishes the quiet bed separately.
				cue.damage = assigned;
				cue.peak_gain = definition.audio_peak_gain;
				cue.decay = definition.audio_decay;
				cue.threshold = definition.audio_threshold;
				cue.full_damage = definition.audio_full_damage;
				emit_audio(ship, std::move(cue));
			}
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
