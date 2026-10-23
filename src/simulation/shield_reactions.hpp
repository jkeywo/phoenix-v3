// Scheduled results are world-owned: destroying the granting ship cannot retract them.
void ensure_shield_reaction_capacity(flecs::entity ship) const
{
	std::size_t possible = 0;
	if (const auto *owner = ship.try_get<ShieldComponent>())
		for (const auto &c : owner->contributions)
		{
			const auto &d = c.definition.get<ShieldDefinition>();
			possible += d.reactions.size() + (d.post_damage ? 1 : 0);
		}
	if (shield_reactions.size() + possible > 8192 || next_shield_hit >= 9007199254740991ULL ||
	    next_shield_reaction + possible >= 9007199254740991ULL)
		throw std::runtime_error("Shield reaction capacity exhausted before damage");
}
void queue_shield_reactions(flecs::entity ship, const ShieldContribution &c, std::uint64_t hit, EntityId attacker,
                            std::int64_t presented, std::int64_t modified, std::int64_t negated, std::int64_t absorbed,
                            std::int64_t residual)
{
	const auto &d = c.definition.get<ShieldDefinition>();
	auto append = [&](ShieldReactionDefinition execution, std::size_t group)
	{
		ShieldReaction r;
		r.id = next_shield_reaction++;
		r.due = ecs.get<Clock>().tick + 1;
		r.hit = hit;
		r.effect = c.effect_id;
		r.attacker = attacker;
		r.protected_ship = ship.get<StableId>().id;
		r.contribution = c.name;
		r.source = c.definition.id;
		r.definition = c.definition_id;
		r.group = group;
		r.presented = presented;
		r.modified = modified;
		r.negated = negated;
		r.absorbed = absorbed;
		r.residual = residual;
		r.strength = c.state.strength;
		r.execution = std::move(execution);
		shield_reactions.push_back(std::move(r));
	};
	for (std::size_t i = 0; i < d.reactions.size(); ++i)
		append(d.reactions[i], i);
	if (d.post_damage)
	{
		ShieldReactionDefinition legacy;
		ShieldReactionOperation operation;
		operation.kind = ShieldReactionKind::Damage;
		operation.target = "attacker";
		operation.damage = d.post_damage;
		legacy.operations.push_back(operation);
		append(std::move(legacy), d.reactions.size());
	}
}
Json save_shield_reactions() const
{
	Json result = {{"next_hit", next_shield_hit},
	               {"next_reaction", next_shield_reaction},
	               {"pending", Json::array()},
	               {"results", Json::array()}};
	for (const auto &[id, status] : shield_reaction_results)
		result["results"].push_back({{"id", id}, {"status", status}});
	for (const auto &r : shield_reactions)
		result["pending"].push_back({{"id", r.id},
		                             {"due", r.due},
		                             {"hit", r.hit},
		                             {"effect", r.effect},
		                             {"attacker", r.attacker},
		                             {"protected", r.protected_ship},
		                             {"contribution", r.contribution},
		                             {"source", r.source},
		                             {"definition", r.definition},
		                             {"group", r.group},
		                             {"presented", r.presented},
		                             {"modified", r.modified},
		                             {"negated", r.negated},
		                             {"absorbed", r.absorbed},
		                             {"residual", r.residual},
		                             {"strength", r.strength}});
	return result;
}
void restore_shield_reactions(const Json &value)
{
	next_shield_hit = checked_integer(value.at("next_hit"), 1, 9007199254740991LL, "shield.next_hit");
	next_shield_reaction = checked_integer(value.at("next_reaction"), 1, 9007199254740991LL, "shield.next_reaction");
	shield_reaction_results.clear();
	if (!value.at("results").is_array() || value.at("results").size() > 1024)
		throw std::runtime_error("Invalid shield reaction results");
	for (const auto &v : value.at("results"))
	{
		const auto id = checked_integer(v.at("id"), 1, next_shield_reaction - 1, "reaction.result.id");
		const auto status = v.at("status").get<std::string>();
		if (status.empty() || status.size() > 512 || !shield_reaction_results.emplace(id, status).second)
			throw std::runtime_error("Invalid shield reaction result");
	}
	const auto &pending = value.at("pending");
	if (!pending.is_array() || pending.size() > 8192)
		throw std::runtime_error("Invalid shield reaction queue");
	std::vector<ShieldReaction> restored;
	std::set<std::uint64_t> ids;
	std::set<std::tuple<std::uint64_t, std::uint64_t, std::string, std::size_t>> executions;
	std::map<std::tuple<std::uint64_t, std::uint64_t, std::string>, std::vector<std::size_t>> groups;
	for (const auto &v : pending)
	{
		ShieldReaction r;
		r.id = checked_integer(v.at("id"), 1, next_shield_reaction - 1, "reaction.id");
		if (!ids.insert(r.id).second || shield_reaction_results.contains(r.id))
			throw std::runtime_error("Duplicate shield reaction");
		r.due = checked_integer(v.at("due"), 0, 9007199254740991LL, "reaction.due");
		r.hit = checked_integer(v.at("hit"), 1, next_shield_hit - 1, "reaction.hit");
		r.effect = checked_integer(v.at("effect"), 1, 9007199254740991LL, "reaction.effect");
		r.attacker = checked_integer(v.at("attacker"), 0, 2000000000, "reaction.attacker");
		r.protected_ship = checked_integer(v.at("protected"), 1, 2000000000, "reaction.protected");
		r.contribution = v.at("contribution").get<std::string>();
		r.source = v.at("source").get<std::string>();
		r.definition = v.at("definition").get<std::string>();
		const auto at = shield_templates.find(r.definition);
		if (at == shield_templates.end() || r.source.empty() || r.source.size() > 512)
			throw std::runtime_error("Invalid queued shield origin");
		const auto &contributions = at->second.shield_contributions;
		const auto c = std::find_if(contributions.begin(), contributions.end(),
		                            [&](const auto &d)
		                            {
			                            return d.name == r.contribution;
		                            });
		if (c == contributions.end())
			throw std::runtime_error("Unknown queued shield contribution");
		r.group = checked_integer(v.at("group"), 0, c->shield.reactions.size(), "reaction.group");
		if (r.group < c->shield.reactions.size())
			r.execution = c->shield.reactions[r.group];
		else
		{
			if (!c->shield.post_damage)
				throw std::runtime_error("Unknown queued shield reaction");
			ShieldReactionOperation operation;
			operation.kind = ShieldReactionKind::Damage;
			operation.target = "attacker";
			operation.damage = c->shield.post_damage;
			r.execution.operations.push_back(operation);
		}
		r.presented = checked_integer(v.at("presented"), 0, 1000000000000LL, "reaction.presented");
		r.modified = checked_integer(v.at("modified"), 0, 100000000000000LL, "reaction.modified");
		r.negated = checked_integer(v.at("negated"), 0, r.presented, "reaction.negated");
		r.absorbed = checked_integer(v.at("absorbed"), 0, c->shield.reserve.capacity, "reaction.absorbed");
		r.residual = checked_integer(v.at("residual"), 0, 100000000000000LL, "reaction.residual");
		r.strength = checked_integer(v.at("strength"), 0, c->shield.reserve.capacity, "reaction.strength");
		if (r.presented == 0 || r.modified + r.negated != r.presented || r.absorbed != r.modified ||
		    r.strength + r.absorbed > c->shield.reserve.capacity ||
		    !executions.emplace(r.hit, r.effect, r.contribution, r.group).second)
			throw std::runtime_error("Inconsistent queued shield result");
		groups[{r.hit, r.effect, r.contribution}].push_back(restored.size());
		restored.push_back(std::move(r));
	}
	for (const auto &[key, indices] : groups)
	{
		const auto &first = restored[indices.front()];
		const auto &definitions = shield_templates.at(first.definition).shield_contributions;
		const auto c = std::find_if(definitions.begin(), definitions.end(),
		                            [&](const auto &d)
		                            {
			                            return d.name == first.contribution;
		                            });
		if (indices.size() != c->shield.reactions.size() + (c->shield.post_damage ? 1 : 0))
			throw std::runtime_error("Incomplete queued shield reaction groups");
		for (const auto index : indices)
		{
			const auto &r = restored[index];
			if (std::tie(r.attacker, r.protected_ship, r.source, r.definition, r.due, r.presented, r.modified,
			             r.negated, r.absorbed, r.residual,
			             r.strength) != std::tie(first.attacker, first.protected_ship, first.source, first.definition,
			                                     first.due, first.presented, first.modified, first.negated,
			                                     first.absorbed, first.residual, first.strength))
				throw std::runtime_error("Mismatched queued shield reaction context");
		}
	}
	std::sort(restored.begin(), restored.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.id < b.id;
	          });
	shield_reactions = std::move(restored);
}
void step_shield_reactions()
{
	std::vector<ShieldReaction> due;
	for (const auto &r : shield_reactions)
		if (r.due <= ecs.get<Clock>().tick)
			due.push_back(r);
	std::sort(due.begin(), due.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.id < b.id;
	          });
	for (const auto &r : due)
	{
		std::erase_if(shield_reactions,
		              [&](const auto &pending)
		              {
			              return pending.id == r.id;
		              });
		shield_reaction_results[r.id] = "failed: required target or grant unavailable";
		while (shield_reaction_results.size() > 1024)
			shield_reaction_results.erase(shield_reaction_results.begin());
		// Prepare complete component and payer candidates before publishing any writes.
		std::map<EntityId, ShieldComponent> staged;
		std::vector<std::pair<EntityId, std::int64_t>> damages;
		bool valid = true;
		for (const auto &operation : r.execution.operations)
		{
			const auto target = operation.target == "attacker" ? r.attacker : r.protected_ship;
			const auto at = entities.find(target);
			if (at == entities.end() || !at->second.has<CombatStructure>() || combat_dead(at->second))
			{
				shield_reaction_results[r.id] = "failed: required target unavailable: " + std::to_string(target);
				valid = false;
				break;
			}
			auto ship = at->second;
			if (operation.kind == ShieldReactionKind::Damage)
			{
				damages.emplace_back(target, operation.damage);
				continue;
			}
			if (!ship.has<ShieldComponent>())
			{
				shield_reaction_results[r.id] = "failed: incompatible grant target";
				valid = false;
				break;
			}
			auto [entry, inserted] = staged.try_emplace(target, ship.get<ShieldComponent>());
			auto &owner = entry->second;
			const auto key = "reaction." + std::to_string(r.protected_ship) + "." + r.source + "." + operation.grant;
			if (key.size() > 512)
			{
				valid = false;
				break;
			}
			auto matches = [&](const auto &c)
			{
				return c.equipment.empty() && c.definition.id == key;
			};
			if (operation.kind == ShieldReactionKind::Remove)
			{
				std::erase_if(owner.contributions, matches);
				continue;
			}
			const auto definition = shield_templates.find(operation.effect);
			if (definition == shield_templates.end() ||
			    std::any_of(owner.contributions.begin(), owner.contributions.end(), matches) ||
			    owner.contributions.size() + definition->second.shield_contributions.size() > 8192 ||
			    owner.next_effect >= 9007199254740991ULL)
			{
				shield_reaction_results[r.id] = "failed: unavailable or duplicate grant";
				valid = false;
				break;
			}
			const auto effect = owner.next_effect++;
			const auto duration = shield_durations.at(operation.effect);
			for (const auto &d : definition->second.shield_contributions)
			{
				ShieldContribution c;
				c.effect_id = effect;
				c.name = d.name;
				c.definition_id = operation.effect;
				c.definition = definition->second;
				c.definition.id = key;
				c.definition.bearing = d.bearing;
				c.definition.arc = d.arc;
				c.definition.details = d.shield;
				c.definition.shield_contributions.clear();
				c.state.strength = d.shield.reserve.initial;
				c.expiry = duration > 0 ? static_cast<std::int64_t>(ecs.get<Clock>().tick) + duration : -1;
				owner.contributions.push_back(std::move(c));
			}
		}
		if (!valid)
			continue;
		// Reserve the worst case before committing a paid operation. Generated work is due next tick.
		std::size_t reserve = 0;
		for (const auto &[id, amount] : damages)
		{
			const auto &owner = staged.contains(id) ? staged.at(id) : entities.at(id).get<ShieldComponent>();
			for (const auto &c : owner.contributions)
			{
				const auto &d = c.definition.get<ShieldDefinition>();
				reserve += d.reactions.size() + (d.post_damage ? 1 : 0);
			}
		}
		if (shield_reactions.size() + reserve > 8192 || next_shield_reaction + reserve >= 9007199254740991ULL ||
		    next_shield_hit + damages.size() >= 9007199254740991ULL)
		{
			shield_reactions.push_back(r);
			shield_reaction_results.erase(r.id);
			std::sort(shield_reactions.begin(), shield_reactions.end(),
			          [](const auto &a, const auto &b)
			          {
				          return a.id < b.id;
			          });
			throw std::runtime_error("Shield reaction capacity exhausted before execution");
		}
		auto payer = entities.find(r.protected_ship);
		std::optional<ResourceNetwork> payment;
		if (r.execution.energy > 0)
		{
			if (payer == entities.end() || !payer->second.has<ShipResources>())
			{
				shield_reaction_results[r.id] = "failed: payer ship unavailable";
				continue;
			}
			const auto fitted = instance(payer->second, r.execution.payer);
			if (!fitted || !fitted->entity.has<ResourceBinding>() || !fitted->entity.get<Equipment>().enabled ||
			    structurally_disabled(payer->second, r.execution.payer) ||
			    fault_effectiveness(payer->second, r.execution.payer) == 0)
			{
				shield_reaction_results[r.id] = "failed: payer equipment unavailable: " + r.execution.payer;
				continue;
			}
			auto &power = payer->second.get_mut<ShipResources>();
			const auto slot = fitted->entity.get<ResourceBinding>().slot;
			const auto store = power.definition.systems.at(slot).store;
			if (store >= power.network.stores().size())
			{
				shield_reaction_results[r.id] = "failed: payer has no energy store";
				continue;
			}
			payment = power.network;
			if (!payment->spend_external(store, r.execution.energy))
			{
				shield_reaction_results[r.id] = "failed: energy payment unavailable";
				continue;
			}
		}
		if (payment)
			payer->second.get_mut<ShipResources>().network = std::move(*payment);
		for (auto &[id, owner] : staged)
			entities.at(id).set<ShieldComponent>(std::move(owner));
		for (const auto &[id, amount] : damages)
		{
			auto ship = entities.at(id);
			auto heading = ship.get<Pose>().heading;
			if (const auto origin = entities.find(r.protected_ship); origin != entities.end())
				heading = combat_bearing(ship.get<Pose>().position - origin->second.get<Pose>().position);
			const auto remaining = absorb_shields(ship, heading, amount, 0, nullptr, r.protected_ship);
			if (remaining)
				combat_damage(ship, 0, remaining);
		}
		shield_reaction_results[r.id] = "committed";
	}
}
