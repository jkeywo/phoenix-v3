// Included inside World::Impl. Outcomes are published after native resolution;
// their groups run in the next tick's scheduled-effect phase, never recursively.
std::map<std::string, PayloadDefinition> payload_templates;
std::map<std::string, PeriodicEffectDefinition> periodic_templates;
std::vector<PeriodicEffectInstance> periodic_effects;
std::vector<PendingPayload> pending_payloads;
std::map<std::uint64_t, std::string> payload_results;
std::uint64_t next_periodic_instance = 1, next_payload_job = 1;

static std::string payload_source_identity(EntityId source, const std::string &equipment, const std::string &payload,
                                           const std::string &binding)
{
	const auto part = [](const std::string &v)
	{
		return std::to_string(v.size()) + ":" + v;
	};
	const auto origin = std::to_string(source) + "." + part(equipment) + "." + part(payload) + "." + part(binding);
	return "payload." +
	       sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(origin.data()), origin.size()));
}

static bool same_payload_context(const PayloadHitContext &a, const PayloadHitContext &b)
{
	return std::tie(a.target, a.kind, a.compartment, a.equipment, a.contribution, a.contribution_name, a.ordnance) ==
	       std::tie(b.target, b.kind, b.compartment, b.equipment, b.contribution, b.contribution_name, b.ordnance);
}
flecs::entity payload_target(const PayloadHitContext &c) const
{
	if (c.ordnance)
	{
		for (auto row : projectile_rows())
			if (row.get<Projectile>().id == c.target)
				return row;
		return {};
	}
	const auto found = entities.find(c.target);
	return found == entities.end() ? flecs::entity{} : found->second;
}
bool payload_context_available(const PayloadHitContext &c) const
{
	const auto ship = payload_target(c);
	if (!ship || combat_dead(ship))
		return false;
	if (c.ordnance)
		return c.kind == PayloadOutcomeKind::Entity && ship.get<Destructible>().durability > 0;
	if (c.kind == PayloadOutcomeKind::Entity)
		return ship.has<Destructible>();
	if (c.kind == PayloadOutcomeKind::Shield)
	{
		const auto *shields = ship.try_get<ShieldComponent>();
		return shields && std::any_of(shields->contributions.begin(), shields->contributions.end(),
		                              [&](const auto &s)
		                              {
			                              return s.effect_id == c.contribution &&
			                                     s.definition.name == c.contribution_name;
		                              });
	}
	const auto *structure = ship.try_get<CombatStructure>();
	if (!structure || c.compartment < 0 || c.compartment >= static_cast<std::int64_t>(structure->compartments.size()))
		return false;
	if (c.kind == PayloadOutcomeKind::System)
	{
		const auto at = structure->definition.equipment_compartments.find(c.equipment);
		return at != structure->definition.equipment_compartments.end() &&
		       structure->definition.compartment_ids.at(at->second) == static_cast<std::size_t>(c.compartment);
	}
	return c.kind == PayloadOutcomeKind::Compartment;
}
std::vector<PayloadDefinition> capture_directed_payloads(flecs::entity ship, const CombatMechanism &mechanism,
                                                         const std::string &resource) const
{
	std::vector<PayloadDefinition> result;
	const auto *mode = directed_mode(ship.get<StableId>().id, mechanism.definition.id);
	if (mode)
		result = mode->payloads;
	const auto append = [&](const PayloadDefinition &payload)
	{
		if (std::none_of(result.begin(), result.end(),
		                 [&](const auto &p)
		                 {
			                 return p.id == payload.id;
		                 }))
			result.push_back(payload);
	};
	if (mechanism.definition.kind == OperatingSystem::Weapon)
	{
		const auto &profile = mode && mode->operation == DirectedOperationKind::Launch
		                          ? weapon_payload(ship, mechanism)
		                          : mechanism.definition.ammunition().projectile;
		for (const auto &payload : profile.payloads)
			append(payload);
	}
	if (mode && mode->operation == DirectedOperationKind::Launch && mode->resource_payloads && !resource.empty())
		for (const auto &effect : resource_catalogue->at(resource).effects)
			if (const auto at = payload_templates.find(effect); at != payload_templates.end())
				append(at->second);
	return result;
}
void queue_payload_outcome(const Projectile &projectile, const PayloadHitContext &context, std::int64_t damage)
{
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	std::vector<PendingPayload> prepared;
	for (const auto &payload : projectile.payloads)
		for (const auto &binding : payload.bindings)
			if (binding.outcome == context.kind && (!binding.positive_damage || damage > 0))
			{
				PendingPayload pending;
				pending.original_source = projectile.source;
				pending.original_equipment = projectile.weapon;
				// Opaque deterministic identity keeps maximum-length authored names
				// within the shared effect source bound, including appended grants.
				pending.source = payload_source_identity(projectile.source, projectile.weapon, payload.id, binding.id);
				validate_effect_identity(pending.source, {}, false);
				pending.payload = payload.id;
				pending.binding = binding.id;
				pending.context = context;
				pending.due = effect_expiry(tick, 1);
				prepared.push_back(std::move(pending));
			}
	if (next_payload_job + prepared.size() >= 9007199254740991ULL)
		throw std::runtime_error("Payload outcome identity exhausted");
	for (auto &pending : prepared)
	{
		pending.id = next_payload_job++;
		if (pending_payloads.size() < 8192)
			pending_payloads.push_back(std::move(pending));
		else
		{
			// Native contact is already committed. Failure of its secondary work
			// is a receipt, never a rollback or an exception half way through damage.
			payload_results[pending.id] = "failed: payload outcome capacity exhausted";
			while (payload_results.size() > 1024)
				payload_results.erase(payload_results.begin());
		}
	}
}
void prepare_periodic_application(std::vector<PeriodicEffectInstance> &prepared, std::uint64_t &next,
                                  const ShieldReactionOperation &operation, const std::string &source,
                                  EntityId original_source, const PayloadHitContext &context,
                                  const std::string &group_source, const std::string &original_equipment)
{
	validate_effect_identity(source, {}, false);
	const auto matches = [&](const auto &c)
	{
		if (operation.all_sources)
			return c.definition == operation.effect && c.context.target == context.target &&
			       c.context.ordnance == context.ordnance;
		return c.source == source && same_payload_context(c.context, context) &&
		       (operation.effect.empty() || c.definition == operation.effect);
	};
	if (operation.kind == ShieldReactionKind::Remove)
	{
		std::erase_if(prepared, matches);
		return;
	}
	if (!payload_context_available(context))
		throw std::runtime_error("Periodic target unavailable");
	const auto &definition = periodic_templates.at(operation.effect);
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	const auto expiry = effect_expiry(tick, definition.duration_ticks);
	const auto found = std::find_if(prepared.begin(), prepared.end(), matches);
	if (found != prepared.end())
	{
		found->expiry = std::max(found->expiry, expiry);
		return; // Preserve cadence, instance identity and already completed ticks.
	}
	if (prepared.size() >= 8192 || next >= 9007199254740991ULL)
		throw std::runtime_error("Periodic instance capacity exhausted");
	prepared.push_back({next++, original_source, source, operation.effect, group_source, original_equipment, context,
	                    expiry, effect_expiry(tick, definition.period_ticks)});
}
void apply_payload_damage(flecs::entity ship, std::int64_t amount, const PayloadHitContext &context)
{
	if (!payload_context_available(context) || amount <= 0)
		return;
	if (context.kind == PayloadOutcomeKind::Shield)
	{
		for (auto &c : ship.get_mut<ShieldComponent>().contributions)
			if (c.effect_id == context.contribution && c.definition.name == context.contribution_name)
				c.state.strength = std::max<std::int64_t>(0, c.state.strength - amount);
	}
	else if (context.kind == PayloadOutcomeKind::System)
		damage_faults(ship, static_cast<std::size_t>(context.compartment), amount, nullptr, context.equipment);
	else if (context.kind == PayloadOutcomeKind::Compartment)
		combat_damage(ship, static_cast<std::size_t>(context.compartment), amount);
	else if (ship.has<CombatStructure>())
	{
		const auto remaining = absorb_shields(ship, ship.get<Pose>().heading, amount, 0);
		if (remaining)
			combat_damage(ship, 0, remaining);
	}
	else if (auto *d = ship.try_get_mut<Destructible>())
	{
		d->durability = std::max<std::int64_t>(0, d->durability - amount);
		if (!d->durability)
			d->destroyed_tick = ecs.get<Clock>().tick;
	}
}
void step_payload_effects()
{
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	std::erase_if(periodic_effects,
	              [&](const auto &c)
	              {
		              return tick >= c.expiry || !payload_context_available(c.context);
	              });
	// Snapshot IDs, not instances: a prior group can explicitly remove an instance
	// before its turn. Refresh does not change its generation or scheduled cadence.
	std::vector<std::uint64_t> periodic_due;
	for (const auto &c : periodic_effects)
		if (c.next <= tick && c.next < c.expiry)
			periodic_due.push_back(c.id);
	std::sort(periodic_due.begin(), periodic_due.end());
	for (auto id : periodic_due)
	{
		const auto found = std::find_if(periodic_effects.begin(), periodic_effects.end(),
		                                [&](const auto &c)
		                                {
			                                return c.id == id;
		                                });
		if (found == periodic_effects.end() || !payload_context_available(found->context))
			continue;
		const auto captured = *found;
		found->next = effect_expiry(found->next, periodic_templates.at(found->definition).period_ticks);
		execute_effect_group(periodic_templates.at(captured.definition).reaction, captured.context.target,
		                     captured.original_source, captured.group_source, &captured.context,
		                     captured.original_equipment);
	}
	std::vector<PendingPayload> due;
	for (const auto &pending : pending_payloads)
		if (pending.due <= tick)
			due.push_back(pending);
	std::sort(due.begin(), due.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.id < b.id;
	          });
	for (const auto &pending : due)
	{
		std::erase_if(pending_payloads,
		              [&](const auto &p)
		              {
			              return p.id == pending.id;
		              });
		std::string failure = "Hit target unavailable";
		if (payload_context_available(pending.context))
		{
			const auto &bindings = payload_templates.at(pending.payload).bindings;
			const auto binding = std::find_if(bindings.begin(), bindings.end(),
			                                  [&](const auto &b)
			                                  {
				                                  return b.id == pending.binding;
			                                  });
			failure = execute_effect_group(binding->reaction, pending.context.target, pending.original_source,
			                               pending.source, &pending.context, pending.original_equipment);
		}
		payload_results[pending.id] = failure.empty() ? "committed" : "failed: " + failure;
		while (payload_results.size() > 1024)
			payload_results.erase(payload_results.begin());
	}
}

Json save_payload_effects() const
{
	return {{"next_instance", next_periodic_instance},
	        {"next_job", next_payload_job},
	        {"active", periodic_effects},
	        {"pending", pending_payloads},
	        {"results", payload_results}};
}
void validate_saved_payload_context(const PayloadHitContext &c, const Json &value) const
{
	checked_integer(value.at("target"), 1, 9007199254740991LL, "payload.target");
	checked_integer(value.at("kind"), 0, static_cast<std::int64_t>(PayloadOutcomeKind::Entity), "payload.kind");
	checked_integer(value.at("compartment"), -1, 1000000, "payload.compartment");
	checked_integer(value.at("contribution"), 0, 9007199254740991LL, "payload.contribution");
	if (Json(c) != value || c.equipment.size() > 256 || c.contribution_name.size() > 256 ||
	    (!c.ordnance && !entities.contains(c.target)))
		throw std::runtime_error("Invalid saved payload context");
	if (c.ordnance)
	{
		if (c.kind != PayloadOutcomeKind::Entity || c.compartment != -1 || !c.equipment.empty() || c.contribution ||
		    !c.contribution_name.empty() || c.target >= next_projectile)
			throw std::runtime_error("Invalid ordnance payload context");
		return; // A projectile can have been removed after the outcome was published.
	}
	const auto ship = entities.at(c.target);
	if (c.kind == PayloadOutcomeKind::Entity)
	{
		if (c.compartment != -1 || !c.equipment.empty() || c.contribution || !c.contribution_name.empty() ||
		    !ship.has<Destructible>())
			throw std::runtime_error("Invalid entity payload context");
	}
	else if (c.kind == PayloadOutcomeKind::Shield)
	{
		// A reached contribution can expire before its pending reaction executes.
		if (c.compartment != -1 || !c.equipment.empty() || !c.contribution || c.contribution_name.empty() ||
		    !ship.has<ShieldComponent>())
			throw std::runtime_error("Invalid shield payload context");
	}
	else
	{
		const auto *structure = ship.try_get<CombatStructure>();
		if (!structure || c.compartment < 0 ||
		    c.compartment >= static_cast<std::int64_t>(structure->compartments.size()) || c.contribution ||
		    !c.contribution_name.empty())
			throw std::runtime_error("Invalid compartment payload context");
		if (c.kind == PayloadOutcomeKind::Compartment && !c.equipment.empty())
			throw std::runtime_error("Unexpected compartment equipment");
		if (c.kind == PayloadOutcomeKind::System)
		{
			const auto at = structure->definition.equipment_compartments.find(c.equipment);
			if (at == structure->definition.equipment_compartments.end() ||
			    structure->definition.compartment_ids.at(at->second) != static_cast<std::size_t>(c.compartment))
				throw std::runtime_error("Invalid system hit binding");
		}
	}
}
void restore_payload_effects(const Json &value)
{
	next_periodic_instance = checked_integer(value.at("next_instance"), 1, 9007199254740991LL, "payload.next_instance");
	next_payload_job = checked_integer(value.at("next_job"), 1, 9007199254740991LL, "payload.next_job");
	if (!value.at("active").is_array() || value.at("active").size() > 8192 || !value.at("pending").is_array() ||
	    value.at("pending").size() > 8192)
		throw std::runtime_error("Invalid payload roster");
	std::vector<PeriodicEffectInstance> restored;
	std::set<std::uint64_t> ids;
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	for (const auto &entry : value.at("active"))
	{
		auto c = entry.get<PeriodicEffectInstance>();
		c.id = checked_integer(entry.at("id"), 1, next_periodic_instance - 1, "periodic.id");
		checked_integer(entry.at("original_source"), 0, 9007199254740991LL, "periodic.source");
		c.expiry = checked_integer(entry.at("expiry"), tick, 9007199254740991LL, "periodic.expiry");
		c.next = checked_integer(entry.at("next"), tick, 9007199254740991LL, "periodic.next");
		validate_effect_identity(c.source, {}, false);
		validate_effect_identity(c.group_source, {}, false);
		if (c.original_equipment.size() > 256)
			throw std::runtime_error("Invalid periodic equipment origin");
		if (!c.source.starts_with(c.group_source + "."))
			throw std::runtime_error("Invalid periodic grant identity");
		validate_saved_payload_context(c.context, entry.at("context"));
		if (!ids.insert(c.id).second || !periodic_templates.contains(c.definition) || Json(c) != entry ||
		    std::any_of(restored.begin(), restored.end(),
		                [&](const auto &p)
		                {
			                return p.source == c.source && p.definition == c.definition &&
			                       same_payload_context(p.context, c.context);
		                }))
			throw std::runtime_error("Invalid periodic continuation");
		const auto &d = periodic_templates.at(c.definition);
		if (c.expiry > tick + d.duration_ticks || c.next > tick + d.period_ticks)
			throw std::runtime_error("Periodic deadline exceeds authored bounds");
		restored.push_back(std::move(c));
	}
	std::vector<PendingPayload> pending;
	ids.clear();
	for (const auto &entry : value.at("pending"))
	{
		auto p = entry.get<PendingPayload>();
		p.id = checked_integer(entry.at("id"), 1, next_payload_job - 1, "payload.id");
		checked_integer(entry.at("original_source"), 0, 9007199254740991LL, "payload.source");
		p.due = checked_integer(entry.at("due"), tick, tick + 1, "payload.due");
		validate_effect_identity(p.source, {}, false);
		if (p.original_equipment.size() > 256 ||
		    p.source != payload_source_identity(p.original_source, p.original_equipment, p.payload, p.binding))
			throw std::runtime_error("Invalid pending payload origin");
		validate_saved_payload_context(p.context, entry.at("context"));
		if (!ids.insert(p.id).second || !payload_templates.contains(p.payload) || Json(p) != entry)
			throw std::runtime_error("Invalid pending payload");
		const auto &bindings = payload_templates.at(p.payload).bindings;
		if (std::none_of(bindings.begin(), bindings.end(),
		                 [&](const auto &b)
		                 {
			                 return b.id == p.binding && b.outcome == p.context.kind;
		                 }))
			throw std::runtime_error("Invalid pending payload binding");
		pending.push_back(std::move(p));
	}
	std::map<std::uint64_t, std::string> results;
	if (!value.at("results").is_array() || value.at("results").size() > 1024)
		throw std::runtime_error("Invalid payload receipts");
	for (const auto &entry : value.at("results"))
	{
		if (!entry.is_array() || entry.size() != 2)
			throw std::runtime_error("Invalid payload receipt shape");
		const auto id = checked_integer(entry.at(0), 1, next_payload_job - 1, "payload.receipt.id");
		const auto status = entry.at(1).get<std::string>();
		if (ids.contains(id) || status.empty() || status.size() > 512 || !results.emplace(id, status).second)
			throw std::runtime_error("Invalid payload receipt");
	}
	periodic_effects = std::move(restored);
	pending_payloads = std::move(pending);
	payload_results = std::move(results);
}
