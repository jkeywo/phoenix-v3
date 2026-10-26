std::int64_t property_fault_delta(flecs::entity ship, const std::string &equipment, PropertyKey property) const
{
	// Output faults affect native throughput, not prices, discharge conversion or speed limits.
	if (property == PropertyKey::sustain || property == PropertyKey::discharge || property == PropertyKey::speed)
		return 0;
	std::int64_t delta = 0;
	if (const auto *structure = ship.try_get<CombatStructure>())
		if (const auto found = structure->faults.find(equipment); found != structure->faults.end())
		{
			const auto &definitions = structure->definition.faults.at(equipment);
			for (std::size_t i = 0; i < found->second.size(); ++i)
				if (found->second[i].active && !definitions[i].destroys && definitions[i].effect != "disabled" &&
				    definitions[i].effect != "feed")
					delta += definitions[i].effectiveness - 1000;
		}
	return delta;
}
bool property_available(flecs::entity ship, const std::string &equipment) const
{
	if (equipment.empty() || equipment.starts_with("effect:") || equipment.starts_with("away:"))
		return true;
	const auto fitted = instance(ship, equipment);
	return fitted && fitted->enabled && !structurally_disabled(ship, equipment);
}
PropertyTargetKey property_target_key(flecs::entity ship, const std::string &name) const
{
	if (const auto *equipment = ship.try_get<EquipmentIndex>())
		if (const auto at = equipment->by_id.find(name); at != equipment->by_id.end())
			return {PropertyTargetKind::Equipment, at->second.id(), {}};
	if (name.starts_with("effect:"))
	{
		const auto separator = name.find(':', 7);
		std::uint64_t id = 0;
		if (separator != std::string::npos)
		{
			const auto parsed = std::from_chars(name.data() + 7, name.data() + separator, id);
			if (parsed.ec == std::errc{} && parsed.ptr == name.data() + separator)
				return {PropertyTargetKind::Shield, id, name.substr(separator + 1)};
		}
	}
	return {PropertyTargetKind::Logical, 0, name};
}
static PropertyTargetKey property_target_key(const EffectTarget &target)
{
	if (target.kind == EffectTargetKind::Equipment)
		return {PropertyTargetKind::Equipment, target.equipment.id(), {}};
	return {PropertyTargetKind::Shield, target.contribution, target.contribution_name};
}
static std::int64_t aggregate_delta(const PropertyAggregate &values, const PropertyTargetKey &equipment,
                                    PropertyKey property, const std::optional<PropertyTargetKey> &contribution)
{
	const auto read = [&](const PropertyTargetKey &target)
	{
		const auto at = values.find(target);
		return at == values.end() ? std::int64_t{0} : at->second[static_cast<std::size_t>(property)];
	};
	return read(equipment) + (!contribution || *contribution == equipment ? 0 : read(*contribution));
}
std::int64_t property_delta(flecs::entity ship, const PropertyTargetKey &equipment, PropertyKey property,
                            const std::optional<PropertyTargetKey> &contribution = {}) const
{
	std::int64_t delta = 0;
	if (const auto *effects = ship.try_get<PropertyEffects>())
	{
		if (!effects->aggregate_valid)
		{
			effects->aggregate.clear();
			for (const auto &[id, effect] : effects->applications)
				for (const auto &v : effect.get<ModifierValues>().values)
					effects->aggregate[property_target_key(effect.get<EffectTarget>())]
					                  [static_cast<std::size_t>(v.property)] += v.delta;
			effects->aggregate_valid = true;
		}
		delta += aggregate_delta(effects->aggregate, equipment, property, contribution);
	}
	const auto *personnel = ship.try_get<PersonnelState>();
	if (!personnel || personnel->officers.empty())
		return delta;
	auto &maintained = personnel_properties[ship.get<StableId>().id];
	if (maintained.revision != personnel_property_revision || personnel_property_mutation_depth)
	{
		maintained.values = collect_personnel_properties(ship);
		maintained.revision = personnel_property_revision;
	}
	return delta + aggregate_delta(maintained.values, equipment, property, contribution);
}
std::int64_t property_delta(flecs::entity ship, const std::string &equipment, PropertyKey property,
                            const std::string &contribution = {}) const
{
	return property_delta(ship, property_target_key(ship, equipment), property,
	                      contribution.empty() ? std::optional<PropertyTargetKey>{}
	                                           : property_target_key(ship, contribution));
}
std::int64_t property_delta(flecs::entity ship, const std::string &equipment, const std::string &property,
                            const std::string &contribution = {}) const
{
	return property_delta(ship, equipment, property_key(property), contribution);
}
std::int64_t property_fault_delta(flecs::entity ship, const std::string &equipment, const std::string &property) const
{
	return property_fault_delta(ship, equipment, property_key(property));
}

std::int64_t effective_property(flecs::entity ship, const std::string &equipment, PropertyKey property,
                                std::int64_t base, std::int64_t extra_delta = 0,
                                const std::string &contribution = {}) const
{
	return apply_property_delta(base,
	                            property_delta(ship, equipment, property, contribution) +
	                                property_fault_delta(ship, equipment, property) + extra_delta,
	                            modifier_properties().at(static_cast<std::size_t>(property)).maximum);
}
std::int64_t effective_property(flecs::entity ship, const std::string &equipment, const std::string &property,
                                std::int64_t base, std::int64_t extra_delta = 0,
                                const std::string &contribution = {}) const
{
	return effective_property(ship, equipment, property_key(property), base, extra_delta, contribution);
}

bool modifier_target_compatible(flecs::entity ship, const std::string &equipment,
                                const ModifierEffectDefinition &definition,
                                const ShieldComponent *staged_shields = nullptr) const
{
	if (!ship.has<PropertyEffects>() || definition.contributions.empty())
		return false;
	if (const auto fitted = instance(ship, equipment))
		return std::all_of(
		    definition.contributions.begin(), definition.contributions.end(),
		    [&](const auto &c)
		    {
			    const auto *directed = directed_definition(ship.get<StableId>().id, equipment);
			    if (!(directed ? property_compatible(c.property, fitted->kind, *directed)
			                   : property_compatible(c.property, fitted->kind)))
				    return false;
			    if (c.property == "processing")
			    {
				    const auto *processors = ship.try_get<ShipProcessors>();
				    if (!processors)
					    return false;
				    return std::any_of(
				        processors->values.begin(), processors->values.end(),
				        [&](const auto &entry)
				        {
					        if (entry.second.definition().equipment != equipment)
						        return false;
					        if (fitted->kind != OperatingSystem::Magazine)
						        return true;
					        const auto &reload =
					            fitted->entity.get<CombatMechanism>().definition.get<MagazineDefinition>().processor;
					        return !reload || reload->id != entry.first;
				        });
			    }
			    if (fitted->kind == OperatingSystem::Team)
				    return fitted->entity.get<CombatMechanism>().definition.get<TeamDefinition>().capabilities.contains(
				        c.property);
			    return true;
		    });
	if (const auto *shields = staged_shields ? staged_shields : ship.try_get<ShieldComponent>())
		for (const auto &c : shields->contributions)
			if (equipment == "effect:" + std::to_string(c.effect_id) + ":" + c.definition.name)
				return std::all_of(definition.contributions.begin(), definition.contributions.end(),
				                   [](const auto &p)
				                   {
					                   return property_compatible(p.property, OperatingSystem::Shield);
				                   });
	return false;
}
std::optional<PreparedModifierApplication> prepared_modifier(const PreparedPropertyEffects &prepared,
                                                             const std::string &source,
                                                             const std::string &equipment) const
{
	const auto key = std::pair{source, equipment};
	std::uint64_t id = 0;
	if (const auto staged = prepared.handles.find(key); staged != prepared.handles.end())
		id = staged->second;
	else
	{
		const auto &owner = prepared.ship.get<PropertyEffects>();
		if (const auto live = owner.grants.find(key); live != owner.grants.end())
			id = live->second;
	}
	if (!id)
		return {};
	if (const auto at = prepared.changes.find(id); at != prepared.changes.end())
		return at->second;
	const auto effect = prepared.ship.get<PropertyEffects>().applications.at(id);
	return PreparedModifierApplication{effect.get<ActiveEffectIdentity>(), effect.get<EffectTarget>(),
	                                   effect.get<EffectLifetime>()};
}
std::uint64_t install_modifier_grant(PreparedPropertyEffects &prepared, const ModifierEffectDefinition &definition,
                                     const std::string &source, const std::string &equipment, std::int64_t tick,
                                     const EffectTarget &target)
{
	validate_effect_identity(source, equipment, true);
	const auto expiry = effect_expiry(tick, definition.duration_ticks);
	if (auto existing = prepared_modifier(prepared, source, equipment))
	{
		if (existing->identity.definition != definition.id)
			throw std::runtime_error("Modifier grant name belongs to another definition");
		existing->lifetime.expiry = refresh_effect_expiry(existing->lifetime.expiry, expiry);
		prepared.changes[existing->identity.id] = *existing;
		return existing->identity.id;
	}
	if (prepared.contribution_count + definition.contributions.size() > 8192 || prepared.next_id >= 9007199254740991ULL)
		throw std::runtime_error("Modifier application capacity exhausted");
	const auto id = prepared.next_id++;
	prepared.changes[id] =
	    PreparedModifierApplication{{id, prepared.ship.get<StableId>().id, source, definition.id}, target, {expiry}};
	prepared.handles[{source, equipment}] = id;
	prepared.contribution_count += definition.contributions.size();
	return id;
}
void remove_modifier_grant(PreparedPropertyEffects &prepared, const std::string &source, const std::string &equipment)
{
	validate_effect_identity(source, equipment, true);
	if (const auto existing = prepared_modifier(prepared, source, equipment))
	{
		prepared.changes[existing->identity.id] = std::nullopt;
		prepared.handles[{source, equipment}] = 0;
		prepared.contribution_count -= modifier_templates.at(existing->identity.definition).contributions.size();
	}
}
PreparedPropertyEffects prepare_property_effects(flecs::entity ship) const
{
	const auto &owner = ship.get<PropertyEffects>();
	return {ship, owner.next_id, owner.contribution_count, {}, {}};
}
EffectTarget resolve_modifier_target(flecs::entity ship, const std::string &equipment,
                                     const ShieldComponent *staged_shields = nullptr) const
{
	EffectTarget target;
	target.body = ship;
	target.equipment_name = equipment;
	if (const auto fitted = instance(ship, equipment))
	{
		target.kind = EffectTargetKind::Equipment;
		target.equipment = fitted->entity;
		return target;
	}
	if (const auto *shields = staged_shields ? staged_shields : ship.try_get<ShieldComponent>())
		for (const auto &c : shields->contributions)
			if (equipment == "effect:" + std::to_string(c.effect_id) + ":" + c.definition.name)
			{
				target.kind = EffectTargetKind::Shield;
				target.contribution = c.effect_id;
				target.contribution_name = c.definition.name;
				return target;
			}
	throw std::runtime_error("Modifier target unavailable at commit");
}
void erase_modifier(flecs::entity effect)
{
	const auto identity = effect.get<ActiveEffectIdentity>();
	const auto target = effect.get<EffectTarget>();
	if (target.body.is_alive())
		if (auto *owner = target.body.try_get_mut<PropertyEffects>())
		{
			owner->applications.erase(identity.id);
			owner->grants.erase({identity.source, target.equipment_name});
			owner->contribution_count -= effect.get<ModifierValues>().values.size();
			owner->aggregate_valid = false;
		}
	effect.destruct();
}
void commit_property_effects(flecs::entity ship, const PreparedPropertyEffects &prepared)
{
	struct Row
	{
		PreparedModifierApplication application;
		ModifierValues values;
	};
	std::vector<Row> created;
	// Prepare new numeric rows before publishing any changes. Other applications
	// remain untouched, including their cached aggregate and native ECS versions.
	for (const auto &[id, change] : prepared.changes)
		if (change && !ship.get<PropertyEffects>().applications.contains(id))
		{
			Row row{*change, {}};
			for (const auto &v : modifier_templates.at(change->identity.definition).contributions)
				row.values.values.push_back({property_key(v.property), v.delta_permille});
			created.push_back(std::move(row));
		}
	for (const auto &[id, change] : prepared.changes)
		if (const auto at = ship.get<PropertyEffects>().applications.find(id);
		    at != ship.get<PropertyEffects>().applications.end())
		{
			const auto effect = at->second;
			if (!change)
				erase_modifier(effect);
			else if (effect.get<EffectLifetime>().expiry != change->lifetime.expiry)
				effect.set<EffectLifetime>(change->lifetime);
		}
	for (auto &row : created)
	{
		const auto &application = row.application;
		auto effect = ecs.entity()
		                  .set<ActiveEffectIdentity>(application.identity)
		                  .set<EffectTarget>(application.target)
		                  .set<EffectLifetime>(application.lifetime)
		                  .set<ModifierValues>(std::move(row.values));
		auto &owner = ship.get_mut<PropertyEffects>();
		owner.applications.emplace(application.identity.id, effect);
		owner.grants.emplace(std::pair{application.identity.source, application.target.equipment_name},
		                     application.identity.id);
		owner.aggregate_valid = false;
	}
	auto &owner = ship.get_mut<PropertyEffects>();
	owner.next_id = prepared.next_id;
	owner.contribution_count = prepared.contribution_count;
}

void step_property_effects()
{
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	std::vector<flecs::entity> expired;
	modifier_query.each(
	    [&](flecs::entity effect, const ActiveEffectIdentity &, const EffectTarget &, const EffectLifetime &life,
	        const ModifierValues &)
	    {
		    if (life.expiry >= 0 && life.expiry <= tick)
			    expired.push_back(effect);
	    });
	for (auto effect : expired)
		erase_modifier(effect);
}
void prune_property_targets()
{
	std::vector<flecs::entity> removed;
	modifier_query.each(
	    [&](flecs::entity effect, const ActiveEffectIdentity &identity, const EffectTarget &target,
	        const EffectLifetime &, const ModifierValues &)
	    {
		    bool available = target.body.is_alive();
		    if (available && target.kind == EffectTargetKind::Equipment)
			    available = target.equipment.is_alive() && target.equipment.has<Equipment>() &&
			                target.equipment.parent() == target.body;
		    if (available)
			    available = modifier_target_compatible(target.body, target.equipment_name,
			                                           modifier_templates.at(identity.definition));
		    if (!available)
			    removed.push_back(effect);
	    });
	for (auto effect : removed)
		erase_modifier(effect);
}
Json save_active_modifiers() const
{
	Json owners = Json::array(), rows = Json::array();
	std::map<EntityId, flecs::entity> ordered;
	property_owners.each(
	    [&](flecs::entity ship, const StableId &id, const PropertyEffects &)
	    {
		    ordered.emplace(id.id, ship);
	    });
	for (const auto &[id, ship] : ordered)
	{
		const auto &owner = ship.get<PropertyEffects>();
		owners.push_back({{"owner", id}, {"next_id", owner.next_id}});
		for (const auto &[application, effect] : owner.applications)
		{
			const auto &identity = effect.get<ActiveEffectIdentity>();
			rows.push_back({{"owner", id},
			                {"id", application},
			                {"source", identity.source},
			                {"definition", identity.definition},
			                {"equipment", effect.get<EffectTarget>().equipment_name},
			                {"expiry", effect.get<EffectLifetime>().expiry}});
		}
	}
	return {{"modifier_owners", owners}, {"modifiers", rows}};
}
void restore_active_modifiers(const Json &value)
{
	const auto &owners = value.at("modifier_owners");
	const auto &rows = value.at("modifiers");
	if (!owners.is_array() || !rows.is_array() || owners.size() > entities.size() || rows.size() > owners.size() * 8192)
		throw std::runtime_error("Invalid active modifier roster");
	std::map<EntityId, PreparedPropertyEffects> prepared;
	for (const auto &owner : owners)
	{
		const auto id = checked_integer(owner.at("owner"), 1, 9007199254740991LL, "modifier.owner");
		if (owner.size() != 2 || !entities.contains(id) || !entities.at(id).has<PropertyEffects>() ||
		    !prepared.try_emplace(id).second)
			throw std::runtime_error("Invalid modifier owner");
		prepared.at(id).ship = entities.at(id);
		prepared.at(id).next_id = checked_integer(owner.at("next_id"), 1, 9007199254740991LL, "modifier.next_id");
	}
	std::size_t expected = 0;
	property_owners.each(
	    [&](const StableId &, const PropertyEffects &)
	    {
		    ++expected;
	    });
	if (prepared.size() != expected)
		throw std::runtime_error("Incomplete modifier owner roster");
	std::set<std::pair<EntityId, std::uint64_t>> identities;
	std::set<std::tuple<EntityId, std::string, std::string>> sources;
	for (const auto &row : rows)
	{
		const auto owner = checked_integer(row.at("owner"), 1, 9007199254740991LL, "modifier.owner");
		auto &target = prepared.at(owner);
		const auto id = checked_integer(row.at("id"), 1, target.next_id - 1, "modifier.id");
		const auto source = row.at("source").get<std::string>();
		const auto definition = row.at("definition").get<std::string>();
		const auto equipment = row.at("equipment").get<std::string>();
		const auto expiry = checked_integer(row.at("expiry"), -1, 9007199254740991LL, "modifier.expiry");
		validate_effect_identity(source, equipment, true);
		const auto &compiled = modifier_templates.at(definition);
		if (row.size() != 6 || !identities.emplace(owner, id).second ||
		    !sources.emplace(owner, source, equipment).second ||
		    !modifier_target_compatible(entities.at(owner), equipment, compiled) ||
		    (compiled.duration_ticks == 0) != (expiry == -1) ||
		    target.contribution_count + compiled.contributions.size() > 8192)
			throw std::runtime_error("Invalid saved modifier application");
		target.changes[id] = PreparedModifierApplication{
		    {static_cast<std::uint64_t>(id), static_cast<EntityId>(owner), source, definition},
		    resolve_modifier_target(entities.at(owner), equipment),
		    {expiry}};
		target.handles[{source, equipment}] = id;
		target.contribution_count += compiled.contributions.size();
	}
	for (const auto &[owner, effects] : prepared)
		commit_property_effects(entities.at(owner), effects);
}
