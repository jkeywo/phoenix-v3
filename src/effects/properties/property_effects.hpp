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
PropertyTargetKey property_target_key(flecs::entity ship, const std::string &name) const
{
	return modifier_target_key(ship, name);
}

PropertyAggregateValue read_property_aggregate(flecs::entity ship, const PropertyTargetKey &key, PropertyKey property,
                                               const std::string &custom, RuleSourceAvailabilityCache &sources,
                                               std::map<std::string, bool> &grants, bool retiring = false) const
{
	return read_modifier_aggregate(ship, key, property, custom, grants,
	                               [this, ship, &sources, retiring](const std::string &source)
	                               {
		                               bool available = rule_grant_available(ship.get<StableId>().id, source, &sources);
		                               if (!available && retiring)
		                               {
			                               const auto *previous =
			                                   readonly_rule_execution(parent_rule_execution_key(source));
			                               available = previous && previous->eligible && previous->active &&
			                                           !previous->stopped && previous->output;
		                               }
		                               return available;
	                               });
}
std::int64_t property_delta(flecs::entity ship, const std::string &equipment, PropertyKey property,
                            const std::string &contribution = {}) const
{
	RuleSourceAvailabilityCache sources;
	std::map<std::string, bool> grants;
	const auto key = property_target_key(ship, equipment);
	auto delta = read_property_aggregate(ship, key, property, {}, sources, grants).delta;
	if (!contribution.empty())
	{
		const auto extra = property_target_key(ship, contribution);
		if (!(extra == key))
			delta += read_property_aggregate(ship, extra, property, {}, sources, grants).delta;
	}
	return delta;
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
	                            modifier_property(property).maximum);
}
std::int64_t effective_property(flecs::entity ship, const std::string &equipment, const std::string &property,
                                std::int64_t base, std::int64_t extra_delta = 0,
                                const std::string &contribution = {}) const
{
	return effective_property(ship, equipment, property_key(property), base, extra_delta, contribution);
}
bool modifier_away_target_available(flecs::entity ship, const std::string &equipment) const
{
	const auto id = ship.get<StableId>().id;
	for (std::size_t mission = 0; mission < away_operations.missions().size(); ++mission)
		if (away_ship(away_operations.definitions()[mission]) == id && away_operations.missions()[mission].deployed())
			for (const auto &[slot, asset] : away_operations.missions()[mission].assignments)
				if (asset.starts_with("officer:") &&
				    equipment == "away:" + away_operations.definitions()[mission].id + ":" + asset)
					return true;
	return false;
}
bool modifier_target_compatible(flecs::entity ship, const std::string &equipment,
                                std::span<const PropertyValue> values) const
{
	if (!ship.has<PropertyEffectOwner>() || values.empty())
		return false;
	const auto custom_actor = custom_property_actor(ship.get<StableId>().id, equipment);
	const auto *custom = custom_property_state(custom_property_address(custom_actor));
	for (const auto &row : values)
		if (!row.custom.empty() && (!custom || !custom->bases.contains(row.custom)))
			return false;
	if (std::all_of(values.begin(), values.end(),
	                [](const auto &row)
	                {
		                return !row.custom.empty();
	                }))
		return true;
	if (equipment.starts_with("away:"))
		return std::all_of(values.begin(), values.end(),
		                   [](const auto &c)
		                   {
			                   return c.property == PropertyKey::repair;
		                   }) &&
		       modifier_away_target_available(ship, equipment);
	if (const auto fitted = instance(ship, equipment))
	{
		const auto *directed = directed_definition(ship.get<StableId>().id, equipment);
		return std::all_of(
		    values.begin(), values.end(),
		    [&](const auto &c)
		    {
			    if (!c.custom.empty())
				    return true;
			    const auto &property = modifier_property(c.property).id;
			    if (!(directed ? property_compatible(property, fitted->kind, *directed)
			                   : property_compatible(property, fitted->kind)))
				    return false;
			    if (c.property == PropertyKey::processing)
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
				        property);
			    return true;
		    });
	}
	if (const auto *shields = ship.try_get<ShieldComponent>())
		for (const auto &c : shields->contributions)
			if (equipment == "effect:" + std::to_string(c.effect_id) + ":" + c.definition.name)
				return std::all_of(values.begin(), values.end(),
				                   [](const auto &p)
				                   {
					                   return property_compatible(modifier_property(p.property).id,
					                                              OperatingSystem::Shield);
				                   });
	return false;
}
std::vector<PropertyValue> modifier_values(const ModifierEffectDefinition &definition) const
{
	return phoenix::modifier_values(definition, *property_catalogue);
}
bool modifier_target_compatible(flecs::entity ship, const std::string &equipment,
                                const ModifierEffectDefinition &definition) const
{
	return modifier_target_compatible(ship, equipment, modifier_values(definition));
}
std::uint64_t install_modifier_grant(PropertyEffects &owner, const ModifierEffectDefinition &definition,
                                     const std::string &source, const std::string &equipment, std::int64_t tick,
                                     bool maintained = false)
{
	return phoenix::install_modifier_grant(owner, definition, *property_catalogue, source, equipment, tick, maintained);
}

PropertyMutationObserver property_mutation_observer()
{
	PropertyMutationObserver observer;
	observer.before_owner_change = [this](flecs::entity ship)
	{
		begin_custom_property_batch();
		for (const auto &address : custom_property_addresses(ship.get<StableId>().id))
			capture_custom_property_owner(address);
	};
	observer.before_removal = [this](const EffectTarget &target)
	{
		mark_custom_property_rates_dirty();
		begin_custom_property_batch();
		if (target.body.is_alive())
			capture_custom_property_owner(
			    custom_property_address(custom_property_actor(target.body.get<StableId>().id, target.equipment_name)),
			    true);
	};
	observer.after_change = [this]
	{
		finish_custom_property_batch();
	};
	return observer;
}
void erase_modifier(flecs::entity effect)
{
	property_applications.erase(effect, property_mutation_observer());
}
void commit_property_effects(flecs::entity ship, const PropertyEffects &snapshot)
{
	property_applications.commit(ecs, ship, snapshot, property_mutation_observer());
}
void step_property_effects()
{
	property_applications.expire(static_cast<std::int64_t>(ecs.get<Clock>().tick), property_mutation_observer());
}
void prune_property_targets()
{
	property_applications.prune(
	    [this](flecs::entity body, const std::string &name)
	    {
		    return (name.starts_with("away:") && modifier_away_target_available(body, name)) ||
		           custom_property_state(custom_property_address(custom_property_actor(body.get<StableId>().id, name)));
	    },
	    property_mutation_observer());
}
Json save_active_modifiers() const
{
	return property_applications.save();
}
void restore_active_modifiers(const Json &saved)
{
	property_applications.restore(
	    ecs, saved, entities.size(),
	    [this](EntityId id)
	    {
		    const auto owner = entities.find(id);
		    return owner == entities.end() ? flecs::entity{} : owner->second;
	    },
	    modifier_templates, *property_catalogue,
	    [this](flecs::entity owner, const std::string &equipment, std::span<const PropertyValue> values)
	    {
		    return modifier_target_compatible(owner, equipment, values);
	    },
	    property_mutation_observer());
}
