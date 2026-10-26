std::int64_t property_fault_delta(flecs::entity ship, const std::string &equipment, const std::string &property) const
{
	// Output faults affect native throughput, not prices, discharge conversion or speed limits.
	if (property == "sustain" || property == "discharge" || property == "speed")
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
static std::int64_t aggregate_delta(const PropertyAggregate &values, const std::string &equipment, PropertyKey property,
                                    const std::string &contribution)
{
	const auto read = [&](const std::string &target)
	{
		const auto at = values.find(target);
		return at == values.end() ? std::int64_t{0} : at->second[static_cast<std::size_t>(property)];
	};
	return read(equipment) + (contribution.empty() || contribution == equipment ? 0 : read(contribution));
}
std::int64_t property_delta(flecs::entity ship, const std::string &equipment, PropertyKey property,
                            const std::string &contribution = {}) const
{
	std::int64_t delta = 0;
	if (const auto *effects = ship.try_get<PropertyEffects>())
	{
		if (!effects->aggregate_valid)
		{
			effects->aggregate.clear();
			for (const auto &c : effects->contributions)
				effects->aggregate[c.equipment][static_cast<std::size_t>(property_key(c.property))] += c.delta_permille;
			effects->aggregate_valid = true;
		}
		delta += aggregate_delta(effects->aggregate, equipment, property, contribution);
	}
	auto &maintained = personnel_properties[ship.get<StableId>().id];
	if (maintained.revision != personnel_property_revision || personnel_property_mutation_depth)
	{
		maintained.values.clear();
		for (const auto &c : collect_personnel_properties(ship))
			maintained.values[c.equipment][static_cast<std::size_t>(property_key(c.property))] += c.delta_permille;
		maintained.revision = personnel_property_revision;
	}
	return delta + aggregate_delta(maintained.values, equipment, property, contribution);
}
std::int64_t property_delta(flecs::entity ship, const std::string &equipment, const std::string &property,
                            const std::string &contribution = {}) const
{
	return property_delta(ship, equipment, property_key(property), contribution);
}
std::int64_t property_fault_delta(flecs::entity ship, const std::string &equipment, PropertyKey property) const
{
	return property_fault_delta(ship, equipment, modifier_properties().at(static_cast<std::size_t>(property)).id);
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
                                const ModifierEffectDefinition &definition) const
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
	if (const auto *shields = ship.try_get<ShieldComponent>())
		for (const auto &c : shields->contributions)
			if (equipment == "effect:" + std::to_string(c.effect_id) + ":" + c.definition.name)
				return std::all_of(definition.contributions.begin(), definition.contributions.end(),
				                   [](const auto &p)
				                   {
					                   return property_compatible(p.property, OperatingSystem::Shield);
				                   });
	return false;
}
std::uint64_t install_modifier_grant(PropertyEffects &owner, const ModifierEffectDefinition &definition,
                                     const std::string &source, const std::string &equipment, std::int64_t tick)
{
	validate_effect_identity(source, equipment, true);
	owner.aggregate_valid = false;
	const auto expiry = effect_expiry(tick, definition.duration_ticks);
	std::uint64_t existing = 0;
	for (const auto &c : owner.contributions)
		if (c.source == source && c.equipment == equipment)
		{
			if (c.definition != definition.id)
				throw std::runtime_error("Modifier grant name belongs to another definition");
			existing = c.id;
		}
	if (existing)
	{
		for (auto &c : owner.contributions)
			if (c.id == existing)
				c.expiry = refresh_effect_expiry(c.expiry, expiry);
		return existing;
	}
	if (owner.contributions.size() + definition.contributions.size() > 8192 || owner.next_id >= 9007199254740991ULL)
		throw std::runtime_error("Modifier application capacity exhausted");
	const auto id = owner.next_id++;
	for (const auto &d : definition.contributions)
		owner.contributions.push_back(
		    {id, source, definition.id, d.name, equipment, d.property, d.delta_permille, expiry, false});
	return id;
}
void remove_modifier_grant(PropertyEffects &owner, const std::string &source, const std::string &equipment)
{
	validate_effect_identity(source, equipment, true);
	owner.aggregate_valid = false;
	std::erase_if(owner.contributions,
	              [&](const auto &c)
	              {
		              return c.source == source && c.equipment == equipment;
	              });
}
void step_property_effects()
{
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	for (const auto &[id, ship] : entities)
		if (auto *owner = ship.try_get_mut<PropertyEffects>())
		{
			owner->aggregate_valid = false;
			std::erase_if(owner->contributions,
			              [&](const auto &c)
			              {
				              return c.expiry >= 0 && c.expiry <= tick;
			              });
		}
}
void prune_property_targets()
{
	for (const auto &[id, ship] : entities)
		if (auto *owner = ship.try_get_mut<PropertyEffects>())
		{
			owner->aggregate_valid = false;
			std::erase_if(owner->contributions,
			              [&](const auto &c)
			              {
				              // Independent lifetime survives source loss, but cannot outlive its actual target.
				              return !modifier_target_compatible(ship, c.equipment,
				                                                 modifier_templates.at(c.definition));
			              });
		}
}
Json save_property_effects(flecs::entity ship) const
{
	const auto &owner = ship.get<PropertyEffects>();
	Json rows = Json::array();
	for (const auto &c : owner.contributions)
		rows.push_back({{"id", c.id},
		                {"source", c.source},
		                {"definition", c.definition},
		                {"name", c.name},
		                {"equipment", c.equipment},
		                {"expiry", c.expiry}});
	return {{"next_id", owner.next_id}, {"contributions", rows}};
}
void restore_property_effects(flecs::entity ship, const Json &value)
{
	PropertyEffects restored;
	restored.next_id = checked_integer(value.at("next_id"), 1, 9007199254740991LL, "modifier.next_id");
	const auto &rows = value.at("contributions");
	if (!rows.is_array() || rows.size() > 8192 || value.size() != 2)
		throw std::runtime_error("Invalid modifier application roster");
	std::map<std::uint64_t, std::vector<PropertyContribution>> groups;
	std::set<std::pair<std::string, std::string>> sources;
	for (const auto &row : rows)
	{
		PropertyContribution c;
		c.id = checked_integer(row.at("id"), 1, restored.next_id - 1, "modifier.id");
		c.source = row.at("source").get<std::string>();
		c.definition = row.at("definition").get<std::string>();
		c.name = row.at("name").get<std::string>();
		c.equipment = row.at("equipment").get<std::string>();
		c.expiry = checked_integer(row.at("expiry"), -1, 9007199254740991LL, "modifier.expiry");
		if (row.size() != 6 || c.source.empty() || c.source.size() > 512 || c.equipment.empty() ||
		    c.equipment.size() > 256)
			throw std::runtime_error("Invalid modifier source identity");
		const auto &definition = modifier_templates.at(c.definition);
		if (!modifier_target_compatible(ship, c.equipment, definition))
			throw std::runtime_error("Incompatible saved modifier target");
		const auto entry = std::find_if(definition.contributions.begin(), definition.contributions.end(),
		                                [&](const auto &d)
		                                {
			                                return d.name == c.name;
		                                });
		if (entry == definition.contributions.end() || (definition.duration_ticks == 0) != (c.expiry == -1))
			throw std::runtime_error("Invalid saved modifier contribution");
		c.property = entry->property;
		c.delta_permille = entry->delta_permille;
		groups[c.id].push_back(c);
		restored.contributions.push_back(std::move(c));
	}
	for (const auto &[id, group] : groups)
	{
		const auto &first = group.front();
		if (!sources.emplace(first.source, first.equipment).second ||
		    group.size() != modifier_templates.at(first.definition).contributions.size())
			throw std::runtime_error("Duplicate or incomplete modifier application");
		std::set<std::string> names;
		for (const auto &c : group)
			if (!names.insert(c.name).second || c.source != first.source || c.definition != first.definition ||
			    c.equipment != first.equipment || c.expiry != first.expiry)
				throw std::runtime_error("Inconsistent modifier application group");
	}
	ship.set<PropertyEffects>(std::move(restored));
}
