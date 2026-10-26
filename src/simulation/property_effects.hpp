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
std::int64_t property_delta(flecs::entity ship, const std::string &equipment, const std::string &property,
                            const std::string &contribution = {}) const
{
	std::int64_t delta = 0;
	RuleSourceAvailabilityCache sources;
	std::map<std::string, bool> grants;
	auto include = [&](const PropertyContribution &c)
	{
		if (c.property != property ||
		    (c.equipment != equipment && (contribution.empty() || c.equipment != contribution)))
			return;
		auto known = grants.find(c.source);
		if (known == grants.end())
			known = grants.emplace(c.source, rule_grant_available(ship.get<StableId>().id, c.source, &sources)).first;
		if (known->second)
			delta += c.delta_permille;
	};
	if (const auto *effects = ship.try_get<PropertyEffects>())
		for (const auto &c : effects->contributions)
			include(c);
	return delta;
}
std::int64_t effective_property(flecs::entity ship, const std::string &equipment, const std::string &property,
                                std::int64_t base, std::int64_t extra_delta = 0,
                                const std::string &contribution = {}) const
{
	// Physical limits match the native authored domains. Recharge queries carry
	// milli-strength; native work retains its higher precision until each owner
	// commits progress. Relative cycle/reload/medical/security rates use permille.
	const auto &descriptors = modifier_properties();
	const auto descriptor = std::find_if(descriptors.begin(), descriptors.end(),
	                                     [&](const auto &d)
	                                     {
		                                     return d.id == property;
	                                     });
	if (descriptor == descriptors.end())
		throw std::runtime_error("Unknown native property");
	return apply_property_delta(base,
	                            property_delta(ship, equipment, property, contribution) +
	                                property_fault_delta(ship, equipment, property) + extra_delta,
	                            descriptor->maximum);
}

bool modifier_target_compatible(flecs::entity ship, const std::string &equipment,
                                const ModifierEffectDefinition &definition) const
{
	if (!ship.has<PropertyEffects>() || definition.contributions.empty())
		return false;
	if (equipment.starts_with("away:"))
	{
		if (!std::all_of(definition.contributions.begin(), definition.contributions.end(),
		                 [](const auto &c)
		                 {
			                 return c.property == "repair";
		                 }))
			return false;
		const auto id = ship.get<StableId>().id;
		for (std::size_t mission = 0; mission < away_missions.size(); ++mission)
			if (away_ship(away_definitions[mission]) == id && away_missions[mission].deployed())
				for (const auto &[slot, asset] : away_missions[mission].assignments)
					if (asset.starts_with("officer:") &&
					    equipment == "away:" + away_definitions[mission].id + ":" + asset)
						return true;
		return false;
	}
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
                                     const std::string &source, const std::string &equipment, std::int64_t tick,
                                     bool maintained = false)
{
	validate_effect_identity(source, equipment, true);
	const auto expiry = effect_expiry(tick, definition.duration_ticks);
	std::uint64_t existing = 0;
	for (const auto &c : owner.contributions)
		if (c.source == source && c.equipment == equipment)
		{
			if (c.definition != definition.id)
				throw std::runtime_error("Modifier grant name belongs to another definition");
			if (c.maintained != maintained)
				throw std::runtime_error("Modifier grant changed lifetime owner");
			existing = c.id;
		}
	if (existing)
	{
		for (auto &c : owner.contributions)
			if (c.id == existing)
				c.expiry = refresh_effect_expiry(c.expiry, expiry);
		return existing;
	}
	const auto count = std::count_if(owner.contributions.begin(), owner.contributions.end(),
	                                 [&](const auto &c)
	                                 {
		                                 return c.maintained == maintained;
	                                 });
	const auto limit = maintained ? maintained_modifier_contribution_limit : independent_modifier_contribution_limit;
	if (static_cast<std::size_t>(count) + definition.contributions.size() > limit ||
	    owner.next_id >= 9007199254740991ULL)
		throw std::runtime_error("Modifier application capacity exhausted");
	const auto id = owner.next_id++;
	for (const auto &d : definition.contributions)
		owner.contributions.push_back(
		    {id, source, definition.id, d.name, equipment, d.property, d.delta_permille, expiry, maintained});
	return id;
}
void remove_modifier_grant(PropertyEffects &owner, const std::string &source, const std::string &equipment)
{
	validate_effect_identity(source, equipment, true);
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
			std::erase_if(owner->contributions,
			              [&](const auto &c)
			              {
				              return c.expiry >= 0 && c.expiry <= tick;
			              });
}
void prune_property_targets()
{
	for (const auto &[id, ship] : entities)
		if (auto *owner = ship.try_get_mut<PropertyEffects>())
		{
			std::map<std::uint64_t, std::vector<const PropertyContribution *>> groups;
			for (const auto &row : owner->contributions)
				groups[row.id].push_back(&row);
			std::set<std::uint64_t> invalid;
			for (const auto &[group, rows] : groups)
			{
				const auto &first = *rows.front();
				auto filtered = modifier_templates.at(first.definition);
				std::set<std::string> names;
				for (const auto *row : rows)
					names.insert(row->name);
				std::erase_if(filtered.contributions,
				              [&](const auto &row)
				              {
					              return !names.contains(row.name);
				              });
				// Validate the actual installed role/property subset, not unrelated template rows.
				if (filtered.contributions.size() != rows.size() ||
				    !modifier_target_compatible(ship, first.equipment, filtered))
					invalid.insert(group);
			}
			std::erase_if(owner->contributions,
			              [&](const auto &row)
			              {
				              return invalid.contains(row.id);
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
		                {"expiry", c.expiry},
		                {"delta_permille", c.delta_permille},
		                {"maintained", c.maintained}});
	return {{"next_id", owner.next_id}, {"contributions", rows}};
}
void restore_property_effects(flecs::entity ship, const Json &value)
{
	PropertyEffects restored;
	restored.next_id = checked_integer(value.at("next_id"), 1, 9007199254740991LL, "modifier.next_id");
	const auto &rows = value.at("contributions");
	if (!rows.is_array() || rows.size() > modifier_contribution_limit || value.size() != 2)
		throw std::runtime_error("Invalid modifier application roster");
	std::map<std::uint64_t, std::vector<PropertyContribution>> groups;
	std::set<std::pair<std::string, std::string>> sources;
	std::size_t independent = 0, maintained = 0;
	for (const auto &row : rows)
	{
		PropertyContribution c;
		c.id = checked_integer(row.at("id"), 1, restored.next_id - 1, "modifier.id");
		c.source = row.at("source").get<std::string>();
		c.definition = row.at("definition").get<std::string>();
		c.name = row.at("name").get<std::string>();
		c.equipment = row.at("equipment").get<std::string>();
		c.expiry = checked_integer(row.at("expiry"), -1, 9007199254740991LL, "modifier.expiry");
		c.delta_permille = checked_integer(row.at("delta_permille"), -100000, 100000, "modifier.delta_permille");
		if (!row.at("maintained").is_boolean())
			throw std::runtime_error("Invalid modifier lifetime owner");
		c.maintained = row.at("maintained").get<bool>();
		if (c.maintained)
			++maintained;
		else
			++independent;
		if (independent > independent_modifier_contribution_limit ||
		    maintained > maintained_modifier_contribution_limit)
			throw std::runtime_error("Modifier application capacity exceeded");
		if (row.size() != 8 || c.source.empty() || c.source.size() > 512 || c.equipment.empty() ||
		    c.equipment.size() > 256)
			throw std::runtime_error("Invalid modifier source identity");
		const auto &definition = modifier_templates.at(c.definition);
		const auto entry = std::find_if(definition.contributions.begin(), definition.contributions.end(),
		                                [&](const auto &d)
		                                {
			                                return d.name == c.name;
		                                });
		if (entry == definition.contributions.end() || (definition.duration_ticks == 0) != (c.expiry == -1))
			throw std::runtime_error("Invalid saved modifier contribution");
		c.property = entry->property;
		groups[c.id].push_back(c);
		restored.contributions.push_back(std::move(c));
	}
	for (const auto &[id, group] : groups)
	{
		const auto &first = group.front();
		if (!sources.emplace(first.source, first.equipment).second)
			throw std::runtime_error("Duplicate modifier application");
		std::set<std::string> names;
		for (const auto &c : group)
			if (!names.insert(c.name).second || c.source != first.source || c.definition != first.definition ||
			    c.equipment != first.equipment || c.expiry != first.expiry || c.maintained != first.maintained)
				throw std::runtime_error("Inconsistent modifier application group");
		auto filtered = modifier_templates.at(first.definition);
		std::erase_if(filtered.contributions,
		              [&](const auto &c)
		              {
			              return !names.contains(c.name);
		              });
		if (filtered.contributions.size() != group.size() ||
		    !modifier_target_compatible(ship, first.equipment, filtered))
			throw std::runtime_error("Incompatible saved modifier target/subset");
	}
	ship.set<PropertyEffects>(std::move(restored));
}
