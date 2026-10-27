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
	if (target.kind == EffectTargetKind::Shield)
		return {PropertyTargetKind::Shield, target.contribution, target.contribution_name};
	return {PropertyTargetKind::Logical, 0, target.equipment_name};
}
static void merge_property_aggregate(PropertyAggregateValue &total, const PropertyAggregateValue &value)
{
	total.delta += value.delta;
	if (value.override_value && (value.priority > total.priority ||
	                             (value.priority == total.priority && value.application > total.application)))
	{
		total.override_value = value.override_value;
		total.priority = value.priority;
		total.application = value.application;
	}
}
const PropertyEffectOwner *property_aggregates(flecs::entity ship) const
{
	const auto *owner = ship.try_get<PropertyEffectOwner>();
	if (!owner || owner->aggregate_valid)
		return owner;
	owner->independent.clear();
	owner->guarded.clear();
	for (const auto &[id, effect] : owner->applications)
	{
		const auto &identity = effect.get<ActiveEffectIdentity>();
		const auto key = property_target_key(effect.get<EffectTarget>());
		auto &totals =
		    identity.source.starts_with("rule.") ? owner->guarded[key][identity.source] : owner->independent[key];
		for (const auto &row : effect.get<ModifierValues>().values)
			if (row.custom.empty())
				totals.native[static_cast<std::size_t>(row.property)] += row.delta;
			else
				merge_property_aggregate(totals.custom[row.custom], {row.delta, row.override_value, row.priority, id});
	}
	owner->aggregate_valid = true;
	return owner;
}
PropertyAggregateValue read_property_aggregate(flecs::entity ship, const PropertyTargetKey &key, PropertyKey property,
                                               const std::string &custom, RuleSourceAvailabilityCache &sources,
                                               std::map<std::string, bool> &grants, bool retiring = false) const
{
	PropertyAggregateValue result;
	const auto *owner = property_aggregates(ship);
	if (!owner)
		return result;
	const auto select = [&](const PropertyTotals &totals)
	{
		if (custom.empty())
			return PropertyAggregateValue{totals.native[static_cast<std::size_t>(property)]};
		const auto at = totals.custom.find(custom);
		return at == totals.custom.end() ? PropertyAggregateValue{} : at->second;
	};
	if (const auto at = owner->independent.find(key); at != owner->independent.end())
		result = select(at->second);
	if (const auto at = owner->guarded.find(key); at != owner->guarded.end())
		for (const auto &[source, totals] : at->second)
		{
			const auto value = select(totals);
			if (!value.delta && !value.override_value)
				continue;
			auto known = grants.find(source);
			if (known == grants.end())
			{
				bool available = rule_grant_available(ship.get<StableId>().id, source, &sources);
				if (!available && retiring)
				{
					const auto *previous = readonly_rule_execution(parent_rule_execution_key(source));
					available =
					    previous && previous->eligible && previous->active && !previous->stopped && previous->output;
				}
				known = grants.emplace(source, available).first;
			}
			if (known->second)
				merge_property_aggregate(result, value);
		}
	return result;
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
	for (std::size_t mission = 0; mission < away_missions.size(); ++mission)
		if (away_ship(away_definitions[mission]) == id && away_missions[mission].deployed())
			for (const auto &[slot, asset] : away_missions[mission].assignments)
				if (asset.starts_with("officer:") && equipment == "away:" + away_definitions[mission].id + ":" + asset)
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
	std::vector<PropertyValue> values;
	values.reserve(definition.contributions.size());
	for (const auto &value : definition.contributions)
		if (property_catalogue->contains(value.property))
			values.push_back(
			    {value.name, PropertyKey::count, value.delta_permille, value.property, value.value, value.priority});
		else
			values.push_back({value.name, property_key(value.property), value.delta_permille});
	return values;
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
	validate_effect_identity(source, equipment, false);
	const auto expiry = effect_expiry(tick, definition.duration_ticks);
	std::size_t count = 0;
	for (auto &application : owner.applications)
	{
		if (application.source == source && application.equipment == equipment)
		{
			if (application.definition != definition.id)
				throw std::runtime_error("Modifier grant name belongs to another definition");
			if (application.modifier.maintained != maintained)
				throw std::runtime_error("Modifier grant changed lifetime owner");
			application.expiry = refresh_effect_expiry(application.expiry, expiry);
			return application.id;
		}
		if (application.modifier.maintained == maintained)
			count += application.modifier.values.size();
	}
	const auto limit = maintained ? maintained_modifier_contribution_limit : independent_modifier_contribution_limit;
	if (count + definition.contributions.size() > limit || owner.next_id >= 9007199254740991ULL)
		throw std::runtime_error("Modifier application capacity exhausted");
	const auto id = owner.next_id++;
	owner.applications.push_back(
	    {id, source, definition.id, equipment, expiry, {maintained, modifier_values(definition)}});
	return id;
}
void remove_modifier_grant(PropertyEffects &owner, const std::string &source, const std::string &equipment)
{
	validate_effect_identity(source, equipment, false);
	std::erase_if(owner.applications,
	              [&](const auto &application)
	              {
		              return application.source == source && application.equipment == equipment;
	              });
}
ModifierApplication read_modifier_application(flecs::entity effect) const
{
	const auto &identity = effect.get<ActiveEffectIdentity>();
	return {identity.id,
	        identity.source,
	        identity.definition,
	        effect.get<EffectTarget>().equipment_name,
	        effect.get<EffectLifetime>().expiry,
	        effect.get<ModifierValues>()};
}
PropertyEffects read_property_effects(flecs::entity ship) const
{
	PropertyEffects snapshot;
	if (const auto *owner = ship.try_get<PropertyEffectOwner>())
	{
		snapshot.next_id = owner->next_id;
		snapshot.applications.reserve(owner->applications.size());
		for (const auto &[id, effect] : owner->applications)
			snapshot.applications.push_back(read_modifier_application(effect));
	}
	return snapshot;
}
EffectTarget resolve_modifier_target(flecs::entity ship, const std::string &equipment) const
{
	EffectTarget target;
	target.body = ship;
	target.equipment_name = equipment;
	if (const auto fitted = instance(ship, equipment))
	{
		target.kind = EffectTargetKind::Equipment;
		target.equipment = fitted->entity;
	}
	else if (const auto key = property_target_key(ship, equipment); key.kind == PropertyTargetKind::Shield)
	{
		target.kind = EffectTargetKind::Shield;
		target.contribution = key.identity;
		target.contribution_name = key.name;
	}
	return target;
}
void erase_modifier(flecs::entity effect)
{
	const auto effect_target = effect.get<EffectTarget>();
	mark_custom_property_rates_dirty();
	begin_custom_property_batch();
	if (effect_target.body.is_alive())
		capture_custom_property_owner(custom_property_address(custom_property_actor(
		                                  effect_target.body.get<StableId>().id, effect_target.equipment_name)),
		                              true);
	const auto target = effect.get<EffectTarget>();
	const auto id = effect.get<ActiveEffectIdentity>().id;
	if (target.body.is_alive())
		if (auto *owner = target.body.try_get_mut<PropertyEffectOwner>())
		{
			const auto &identity = effect.get<ActiveEffectIdentity>();
			owner->grants.erase({identity.source, target.equipment_name});
			owner->applications.erase(id);
			owner->aggregate_valid = false;
		}
	effect.destruct();
	finish_custom_property_batch();
}
void commit_property_effects(flecs::entity ship, const PropertyEffects &snapshot)
{
	begin_custom_property_batch();
	for (const auto &address : custom_property_addresses(ship.get<StableId>().id))
		capture_custom_property_owner(address);
	std::set<std::uint64_t> retained;
	for (const auto &application : snapshot.applications)
		retained.insert(application.id);
	std::vector<flecs::entity> removed;
	for (const auto &[id, effect] : ship.get<PropertyEffectOwner>().applications)
		if (!retained.contains(id))
			removed.push_back(effect);
	for (auto effect : removed)
		erase_modifier(effect);
	for (const auto &application : snapshot.applications)
	{
		const auto &applications = ship.get<PropertyEffectOwner>().applications;
		if (const auto at = applications.find(application.id); at != applications.end())
		{
			auto effect = at->second;
			if (effect.get<EffectLifetime>().expiry != application.expiry)
				effect.set<EffectLifetime>({application.expiry});
			if (!(effect.get<ModifierValues>() == application.modifier))
			{
				effect.set<ModifierValues>(application.modifier);
				ship.get_mut<PropertyEffectOwner>().aggregate_valid = false;
			}
		}
		else
		{
			auto effect = ecs.entity()
			                  .set<ActiveEffectIdentity>(
			                      {application.id, ship.get<StableId>().id, application.source, application.definition})
			                  .set<EffectTarget>(resolve_modifier_target(ship, application.equipment))
			                  .set<EffectLifetime>({application.expiry})
			                  .set<ModifierValues>(application.modifier);
			auto &owner = ship.get_mut<PropertyEffectOwner>();
			owner.applications.emplace(application.id, effect);
			owner.grants.emplace(std::pair{application.source, application.equipment}, effect);
			owner.aggregate_valid = false;
		}
	}
	ship.get_mut<PropertyEffectOwner>().next_id = snapshot.next_id;
	finish_custom_property_batch();
}
void step_property_effects()
{
	std::vector<flecs::entity> expired;
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	modifier_query.each(
	    [&](flecs::entity e, const ActiveEffectIdentity &, const EffectTarget &, const EffectLifetime &life,
	        const ModifierValues &)
	    {
		    if (life.expiry >= 0 && life.expiry <= tick)
			    expired.push_back(e);
	    });
	for (auto effect : expired)
		erase_modifier(effect);
}
void prune_property_targets()
{
	std::vector<flecs::entity> invalid;
	modifier_query.each(
	    [&](flecs::entity effect, const ActiveEffectIdentity &, const EffectTarget &target, const EffectLifetime &,
	        const ModifierValues &)
	    {
		    bool available = target.body.is_alive();
		    if (available && target.kind == EffectTargetKind::Equipment)
			    available = target.equipment.is_alive() && target.equipment.has<Equipment>() &&
			                target.equipment.parent() == target.body;
		    if (available && target.kind == EffectTargetKind::Shield)
		    {
			    const auto *shields = target.body.try_get<ShieldComponent>();
			    available = shields && std::any_of(shields->contributions.begin(), shields->contributions.end(),
			                                       [&](const auto &c)
			                                       {
				                                       return c.effect_id == target.contribution &&
				                                              c.definition.name == target.contribution_name;
			                                       });
		    }
		    if (available && target.kind == EffectTargetKind::Logical)
			    available = (target.equipment_name.starts_with("away:") &&
			                 modifier_away_target_available(target.body, target.equipment_name)) ||
			                custom_property_state(custom_property_address(
			                    custom_property_actor(target.body.get<StableId>().id, target.equipment_name)));
		    if (!available)
			    invalid.push_back(effect);
	    });
	for (auto effect : invalid)
		erase_modifier(effect);
}
Json save_active_modifiers() const
{
	Json owners = Json::array(), applications = Json::array();
	std::map<EntityId, flecs::entity> ordered;
	property_owners.each(
	    [&](flecs::entity ship, const StableId &id, const PropertyEffectOwner &)
	    {
		    ordered.emplace(id.id, ship);
	    });
	for (const auto &[id, ship] : ordered)
	{
		const auto &owner = ship.get<PropertyEffectOwner>();
		owners.push_back({{"owner", id}, {"next_id", owner.next_id}});
		for (const auto &[key, effect] : owner.applications)
		{
			const auto &identity = effect.get<ActiveEffectIdentity>();
			const auto &values = effect.get<ModifierValues>();
			Json named = Json::array();
			for (const auto &v : values.values)
				named.push_back({{"name", v.name}, {"delta_permille", v.delta}});
			applications.push_back({{"owner", id},
			                        {"id", key},
			                        {"source", identity.source},
			                        {"definition", identity.definition},
			                        {"equipment", effect.get<EffectTarget>().equipment_name},
			                        {"expiry", effect.get<EffectLifetime>().expiry},
			                        {"maintained", values.maintained},
			                        {"values", named}});
		}
	}
	return {{"modifier_owners", owners}, {"modifiers", applications}};
}
void restore_active_modifiers(const Json &saved)
{
	if (saved.size() != 4 || !saved.at("modifier_owners").is_array() || !saved.at("modifiers").is_array() ||
	    saved.at("modifier_owners").size() > entities.size())
		throw std::runtime_error("Invalid active modifier roster");
	std::map<EntityId, PropertyEffects> staged;
	for (const auto &owner : saved.at("modifier_owners"))
	{
		const auto id = checked_integer(owner.at("owner"), 1, 9007199254740991LL, "modifier.owner");
		if (owner.size() != 2 || !entities.contains(id) || !entities.at(id).has<PropertyEffectOwner>() ||
		    !staged
		         .emplace(id, PropertyEffects{static_cast<std::uint64_t>(checked_integer(
		                                          owner.at("next_id"), 1, 9007199254740991LL, "modifier.next_id")),
		                                      {}})
		         .second)
			throw std::runtime_error("Invalid modifier owner");
	}
	std::size_t expected = 0;
	property_owners.each(
	    [&](const StableId &, const PropertyEffectOwner &)
	    {
		    ++expected;
	    });
	if (staged.size() != expected || saved.at("modifiers").size() > expected * modifier_contribution_limit)
		throw std::runtime_error("Incomplete modifier owner roster");
	std::set<std::pair<EntityId, std::uint64_t>> identities;
	for (const auto &row : saved.at("modifiers"))
	{
		const auto owner = checked_integer(row.at("owner"), 1, 9007199254740991LL, "modifier.owner");
		const auto id = checked_integer(row.at("id"), 1, 9007199254740991LL, "modifier.id");
		if (row.size() != 8 || !staged.contains(owner) || !identities.emplace(owner, id).second ||
		    !row.at("values").is_array() || row.at("values").empty() || row.at("values").size() > 64)
			throw std::runtime_error("Invalid modifier application");
		auto &properties = staged.at(owner);
		ModifierApplication application;
		application.id = checked_integer(row.at("id"), 1, properties.next_id - 1, "modifier.id");
		application.source = row.at("source").get<std::string>();
		application.definition = row.at("definition").get<std::string>();
		application.equipment = row.at("equipment").get<std::string>();
		validate_effect_identity(application.source, application.equipment, false);
		application.expiry = checked_integer(row.at("expiry"), -1, 9007199254740991LL, "modifier.expiry");
		if (!row.at("maintained").is_boolean())
			throw std::runtime_error("Invalid modifier lifetime owner");
		application.modifier.maintained = row.at("maintained").get<bool>();
		const auto &definition = modifier_templates.at(application.definition);
		if ((definition.duration_ticks == 0) != (application.expiry == -1))
			throw std::runtime_error("Invalid saved modifier expiry");
		std::set<std::string> names;
		for (const auto &value : row.at("values"))
		{
			if (value.size() != 2)
				throw std::runtime_error("Invalid modifier named value");
			const auto name = value.at("name").get<std::string>();
			const auto entry = std::find_if(definition.contributions.begin(), definition.contributions.end(),
			                                [&](const auto &d)
			                                {
				                                return d.name == name;
			                                });
			if (entry == definition.contributions.end() || !names.insert(name).second)
				throw std::runtime_error("Invalid saved modifier contribution");
			const auto delta = checked_integer(value.at("delta_permille"), -100000, 100000, "modifier.delta_permille");
			if (property_catalogue->contains(entry->property))
				application.modifier.values.push_back(
				    {name, PropertyKey::count, delta, entry->property, entry->value, entry->priority});
			else
				application.modifier.values.push_back({name, property_key(entry->property), delta});
		}
		if (!modifier_target_compatible(entities.at(owner), application.equipment, application.modifier.values))
			throw std::runtime_error("Incompatible saved modifier target/subset");
		properties.applications.push_back(std::move(application));
	}
	for (const auto &[owner, properties] : staged)
	{
		std::size_t independent = 0, maintained = 0;
		std::set<std::pair<std::string, std::string>> grants;
		for (const auto &application : properties.applications)
		{
			if (!grants.emplace(application.source, application.equipment).second)
				throw std::runtime_error("Duplicate modifier application");
			(application.modifier.maintained ? maintained : independent) += application.modifier.values.size();
		}
		if (independent > independent_modifier_contribution_limit ||
		    maintained > maintained_modifier_contribution_limit)
			throw std::runtime_error("Modifier application capacity exceeded");
		commit_property_effects(entities.at(owner), properties);
	}
}
