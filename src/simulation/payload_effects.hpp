// Native delivery and duration owners; shared rules own reactions and clocks.
std::map<std::string, PayloadDefinition> payload_templates;
std::map<std::string, PeriodicEffectDefinition> periodic_templates;
// Active condition entities own their clocks and captured origin. Ordered handles
// are a query-derived cache; stable effect IDs govern execution and persistence.
const std::vector<flecs::entity> &active_rule_rows() const
{
	if (active_rule_order_dirty)
	{
		active_rule_order.clear();
		active_rule_query.each(
		    [&](flecs::entity e, const ActiveRuleEffect &)
		    {
			    active_rule_order.push_back(e);
		    });
		std::sort(active_rule_order.begin(), active_rule_order.end(),
		          [](auto a, auto b)
		          {
			          return a.template get<ActiveRuleEffect>().id < b.template get<ActiveRuleEffect>().id;
		          });
		active_rule_order_dirty = false;
	}
	return active_rule_order;
}
std::vector<ActiveRuleEffect> active_rule_snapshot() const
{
	std::vector<ActiveRuleEffect> values;
	for (auto effect_entity : active_rule_rows())
	{
		const auto &effect = effect_entity.get<ActiveRuleEffect>();
		values.push_back(effect);
	}
	return values;
}
flecs::entity active_rule_entity(std::uint64_t id) const
{
	const auto at = active_rule_index.find(id);
	return at == active_rule_index.end() ? flecs::entity{} : at->second;
}
void erase_active_rule_effect(flecs::entity entity)
{
	rule_clock_capacity_cache.reset();
	active_rule_index.erase(entity.get<ActiveRuleEffect>().id);
	entity.destruct();
	active_rule_order_dirty = true;
}
template <typename Predicate> std::size_t erase_active_rule_effects_if(Predicate predicate)
{
	std::vector<flecs::entity> removed;
	for (auto entity : active_rule_rows())
		if (predicate(entity.get<ActiveRuleEffect>()))
			removed.push_back(entity);
	for (auto entity : removed)
		erase_active_rule_effect(entity);
	return removed.size();
}
void commit_active_rule_effects(const std::vector<ActiveRuleEffect> &prepared)
{
	rule_clock_capacity_cache.reset();
	std::set<std::uint64_t> retained;
	for (const auto &effect : prepared)
		retained.insert(effect.id);
	erase_active_rule_effects_if(
	    [&](const auto &effect)
	    {
		    return !retained.contains(effect.id);
	    });
	for (const auto &effect : prepared)
		if (auto entity = active_rule_entity(effect.id))
		{
			if (!(entity.get<ActiveRuleEffect>() == effect))
				entity.set<ActiveRuleEffect>(effect);
		}
		else
		{
			auto created =
			    ecs.entity().set<ActiveRuleEffect>(effect).set<ActiveRuleTarget>({payload_target(effect.context)});
			active_rule_index.emplace(effect.id, created);
			active_rule_order_dirty = true;
		}
}
bool active_rule_effect_exists(const std::vector<ActiveRuleEffect> *prepared, const std::string &source,
                               EntityId target) const
{
	if (prepared)
		return std::any_of(prepared->begin(), prepared->end(),
		                   [&](const auto &effect)
		                   {
			                   return effect.source == source && effect.context.target == target;
		                   });
	for (auto effect_entity : active_rule_rows())
	{
		const auto &effect = effect_entity.get<ActiveRuleEffect>();
		if (effect.source == source && effect.context.target == target)
			return true;
	}
	return false;
}
bool active_rule_target_available(const ActiveRuleEffect &effect) const
{
	const auto entity = active_rule_entity(effect.id);
	if (!entity)
		return false;
	const auto body = entity.get<ActiveRuleTarget>().body;
	return body.is_alive() && payload_context_available(effect.context, body);
}
std::uint64_t next_rule_instance = 1;

std::vector<RuleFactBinding> payload_capture_layout(const std::vector<std::string> &payloads) const
{
	std::vector<RuleFactBinding> result;
	std::set<std::pair<std::string, bool>> seen_rules;
	std::set<std::string> seen_effects;
	const auto add = [&](const RuleFactBinding &value)
	{
		if (std::find(result.begin(), result.end(), value) != result.end())
			return;
		if (result.size() >= rule_expression_node_limit)
			throw std::runtime_error("Projectile rule capture layout exceeds supported facts");
		result.push_back(value);
	};
	const auto visit = [&](auto &&self, const std::string &id, bool owner) -> void
	{
		if (!seen_rules.emplace(id, owner).second)
			return;
		const auto &rule = rule_templates.at(id);
		for (const auto &field : rule.facts)
			if (field.role == RuleRole::Source || field.role == RuleRole::Instigator ||
			    (owner && field.role == RuleRole::Owner))
				add(field);
		for (const auto &op : rule.effects.operations)
			if (op.kind == EffectOperationKind::Apply && seen_effects.insert(op.effect).second)
			{
				if (const auto native = native_effect_rules.find(op.effect); native != native_effect_rules.end())
					for (const auto &binding : native->second)
						self(self, binding.rule, false);
				if (const auto active = periodic_templates.find(op.effect); active != periodic_templates.end())
					for (const auto &binding : active->second.rules)
						self(self, binding.rule, false);
			}
	};
	for (const auto &payload : payloads)
		for (const auto &binding : payload_templates.at(payload).rules)
			visit(visit, binding.rule, true);
	return result;
}
std::vector<RuleCapturedFact> capture_payload_rule_facts(EntityId source, const std::string &equipment,
                                                         const std::vector<std::string> &payloads) const
{
	NativeRuleContext context;
	context.owner = context.source = context.instigator = {source, equipment};
	context.tick = ecs.get<Clock>().tick;
	context.capture_owner = true;
	std::vector<RuleCapturedFact> result;
	for (const auto &field : payload_capture_layout(payloads))
		result.push_back({field, read_rule_fact(field, context)});
	return result;
}
void validate_rule_captures(const std::vector<RuleCapturedFact> &captures,
                            const std::vector<std::string> &payloads) const
{
	const auto layout = payload_capture_layout(payloads);
	if (captures.size() != layout.size())
		throw std::runtime_error("Invalid projectile captured rule facts");
	for (std::size_t i = 0; i < captures.size(); ++i)
	{
		const auto &capture = captures[i];
		if (capture.fact != layout[i] || capture.value.number < -9007199254740991LL ||
		    capture.value.number > 9007199254740991LL || capture.value.text.size() > 4096 ||
		    (capture.value.type != state_tree::Type::Missing && capture.value.type != capture.fact.type) ||
		    (capture.value.type == state_tree::Type::Boolean && capture.value.number != 0 &&
		     capture.value.number != 1) ||
		    (capture.value.type != state_tree::Type::Text && !capture.value.text.empty()))
			throw std::runtime_error("Invalid captured rule value/type");
	}
}
static RuleTrigger payload_trigger(PayloadOutcomeKind kind)
{
	switch (kind)
	{
	case PayloadOutcomeKind::Shield:
		return RuleTrigger::PostShieldDamage;
	case PayloadOutcomeKind::Compartment:
		return RuleTrigger::PostCompartmentDamage;
	case PayloadOutcomeKind::System:
		return RuleTrigger::PostSystemDamage;
	case PayloadOutcomeKind::Entity:
		return RuleTrigger::PostEntityDamage;
	}
	throw std::runtime_error("Unknown payload outcome");
}
void queue_payload_outcome(const Projectile &projectile, const PayloadHitContext &hit, std::int64_t damage)
{
	for (const auto &payload : projectile.payloads)
		for (const auto &binding : payload_templates.at(payload).rules)
			if (rule_templates.at(binding.rule).trigger == payload_trigger(hit.kind))
			{
				NativeRuleContext context;
				context.owner = context.source = context.instigator = {projectile.source, projectile.weapon};
				context.subject = context.target = rule_hit_actor(hit);
				context.hit = hit;
				context.tick = ecs.get<Clock>().tick;
				context.elapsed = projectile.age;
				context.presented = context.modified = context.inflicted = damage;
				context.absorbed = hit.kind == PayloadOutcomeKind::Shield ? damage : 0;
				context.captured = projectile.rule_captures;
				context.capture_owner = true;
				if (!rule_binding_matches(binding, context))
					continue;
				context = bind_rule_context(binding, std::move(context));
				const auto source =
				    "payload." + std::to_string(projectile.source) + "." + projectile.weapon + "." + payload;
				enqueue_rule(binding.rule, rule_key(source, binding.id, context.subject), context,
				             RuleEventPhase::Payload, context.tick + 1);
			}
}

void prepare_active_rule_effect(std::vector<ActiveRuleEffect> &prepared, std::uint64_t &next,
                                const EffectOperation &operation, const std::string &source, EntityId original_source,
                                const PayloadHitContext &context, const std::string &group_source,
                                const std::string &original_equipment, const NativeRuleContext *origin = nullptr)
{
	validate_effect_identity(source, {}, false);
	const auto matches = [&](const auto &effect)
	{
		return operation.all_sources
		           ? effect.definition == operation.effect && effect.context.target == context.target &&
		                 effect.context.ordnance == context.ordnance
		           : effect.source == source && same_payload_context(effect.context, context) &&
		                 (operation.effect.empty() || effect.definition == operation.effect);
	};
	if (operation.kind == EffectOperationKind::Remove)
	{
		std::erase_if(prepared, matches);
		return;
	}
	if (!payload_context_available(context))
		throw std::runtime_error("Active rule target unavailable");
	const auto &definition = periodic_templates.at(operation.effect);
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	const auto expiry = effect_expiry(tick, definition.duration_ticks);
	const auto found = std::find_if(prepared.begin(), prepared.end(), matches);
	if (found != prepared.end())
	{
		found->expiry = std::max(found->expiry, expiry);
		if (origin)
		{
			found->origin = *origin;
			found->origin.capture_owner = false;
		}
		return;
	}
	if (prepared.size() >= rule_instance_limit || next >= 9007199254740991ULL)
		throw std::runtime_error("Active rule capacity exhausted");
	ActiveRuleEffect effect;
	effect.id = next++;
	effect.source = source;
	effect.definition = operation.effect;
	effect.group_source = group_source;
	effect.context = context;
	effect.expiry = expiry;
	if (origin)
		effect.origin = *origin;
	else
		effect.origin.source = effect.origin.instigator = {original_source, original_equipment};
	effect.origin.owner = effect.origin.subject = effect.origin.target = rule_hit_actor(context);
	effect.origin.hit = context;
	effect.origin.capture_owner = false;
	prepared.push_back(std::move(effect));
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
bool payload_context_available(const PayloadHitContext &c, flecs::entity resolved = {}) const
{
	const auto ship = resolved ? resolved : payload_target(c);
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
std::vector<std::string> capture_directed_payloads(flecs::entity ship, const CombatMechanism &mechanism,
                                                   const std::string &resource) const
{
	std::vector<std::string> result;
	const auto *mode = directed_mode(ship.get<StableId>().id, mechanism.definition.id);
	if (mode)
		result = mode->payloads;
	const auto append = [&](const std::string &payload)
	{
		if (std::find(result.begin(), result.end(), payload) == result.end())
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
				append(at->first);
	return result;
}
