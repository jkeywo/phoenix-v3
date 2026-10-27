// Included inside World::Impl. Processes reuse the shared expression/payment
// boundary; native mechanisms remain the only owners of physical progress.
ProcessRuntime *process_runtime(const CustomPropertyAddress &address) const
{
	return native_owner_state(address, &OfficerState::processes, &CompartmentState::processes);
}
ProcessState *process_state(const CustomPropertyAddress &address, const std::string &id) const
{
	auto *runtime = process_runtime(address);
	if (!runtime)
		return nullptr;
	const auto at = runtime->processes.find(id);
	return at == runtime->processes.end() ? nullptr : &at->second;
}
const ProcessDefinition &process_definition(const ProcessState &state) const
{
	return process_catalog->at(state.definition);
}
static RuleActor process_actor(const CustomPropertyAddress &address)
{
	return {address.entity,
	        address.equipment,
	        address.officer,
	        false,
	        address.compartment >= 0 ? PayloadOutcomeKind::Compartment : PayloadOutcomeKind::Entity,
	        address.compartment};
}
static std::string process_owner_kind(const CustomPropertyAddress &address)
{
	return !address.equipment.empty() ? "equipment"
	       : !address.officer.empty() ? "officer"
	       : address.compartment >= 0 ? "compartment"
	                                  : "entity";
}
static std::string process_owner_name(const CustomPropertyAddress &address)
{
	return !address.equipment.empty() ? address.equipment
	       : !address.officer.empty() ? address.officer
	       : address.compartment >= 0 ? std::to_string(address.compartment)
	                                  : std::to_string(address.entity);
}
static std::string process_instance_key(const CustomPropertyAddress &address, const ProcessState &state)
{
	const auto value = std::to_string(address.entity) + ":" + process_owner_kind(address) + ":" +
	                   process_owner_name(address) + ":" + state.id + ":" + std::to_string(state.generation);
	return "process." +
	       sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(value.data()), value.size()));
}
static std::string process_run_source(const CustomPropertyAddress &address, const ProcessState &state)
{
	return process_instance_key(address, state) + ".run." + std::to_string(state.run);
}
NativeRuleContext process_context(const CustomPropertyAddress &address, const ProcessState &state) const
{
	auto context = state.origin;
	const auto owner = process_actor(address);
	context.owner = context.source = context.subject = context.target = owner;
	if (!context.instigator.entity)
		context.instigator = owner;
	context.owner_grant = process_run_source(address, state);
	context.owner_effect_kind = NativeEffectKind::Process;
	context.owner_effect_id = state.generation;
	context.hit = rule_hit_context(owner);
	context.tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	context.property_role.clear();
	context.property_filter.clear();
	context.capture_owner = false;
	context.captured.clear();
	return context;
}
EffectGroupInvocation process_effect_invocation(const CustomPropertyAddress &address, const ProcessState &state,
                                                const EffectGroupDefinition &group, std::int64_t output = 1000) const
{
	EffectGroupInvocation frame;
	frame.execution = group;
	frame.protected_ship = frame.attacker = address.entity;
	frame.original_equipment = address.equipment;
	frame.source = process_run_source(address, state);
	frame.rule_context = process_context(address, state);
	frame.hit_context = frame.rule_context->hit;
	frame.output_permille = output;
	return frame;
}
std::string process_execute_groups(const CustomPropertyAddress &address, const ProcessState &state,
                                   std::span<const EffectGroupDefinition> groups, std::int64_t output = 1000)
{
	std::vector<EffectGroupInvocation> frames;
	for (const auto &group : groups)
		frames.push_back(process_effect_invocation(address, state, group, output));
	if (frames.empty())
		return {};
	auto quote = prepare_effect_groups(frames);
	return commit_effect_group(quote);
}
std::string process_execute_lifecycle(const CustomPropertyAddress &address, const ProcessState &state,
                                      const std::optional<EffectGroupDefinition> &group)
{
	if (!group)
		return {};
	return process_execute_groups(address, state, std::span<const EffectGroupDefinition>(&*group, 1));
}
const std::vector<ProcessBinding> &process_owner_bindings(const CustomPropertyAddress &address) const
{
	static const std::vector<ProcessBinding> empty;
	const auto &compiled = definition(address.entity).processes;
	if (!address.equipment.empty())
	{
		const auto at = compiled.equipment.find(address.equipment);
		return at == compiled.equipment.end() ? empty : at->second;
	}
	if (!address.officer.empty())
	{
		const auto at = compiled.officers.find(address.officer);
		return at == compiled.officers.end() ? empty : at->second;
	}
	if (address.compartment >= 0)
	{
		const auto &native = definition(address.entity).combat;
		if (!native || static_cast<std::size_t>(address.compartment) >= native->compartments.size())
			return empty;
		const auto at = compiled.compartments.find(native->compartments[address.compartment].id);
		return at == compiled.compartments.end() ? empty : at->second;
	}
	return compiled.entity;
}
ProcessRuntime initial_process_runtime(const CustomPropertyAddress &address) const
{
	ProcessRuntime runtime;
	for (const auto &binding : process_owner_bindings(address))
	{
		const auto &definition = process_catalog->at(binding.process);
		ProcessState state;
		state.id = state.slot = binding.id;
		state.definition = binding.process;
		state.native_id = binding.native_id;
		state.requested = binding.requested;
		state.phase = state.requested ? "paused" : "off";
		state.generation = runtime.next_generation++;
		runtime.processes.emplace(state.id, std::move(state));
	}
	return runtime;
}
void initialise_processes(flecs::entity ship)
{
	const auto id = ship.get<StableId>().id;
	ship.set<ProcessRuntime>(initial_process_runtime({id}));
	if (const auto *index = ship.try_get<EquipmentIndex>())
		for (const auto &[name, child] : index->by_id)
			child.set<ProcessRuntime>(initial_process_runtime({id, name}));
	if (auto *personnel = ship.try_get_mut<PersonnelState>())
		for (std::size_t i = 0; i < personnel->officers.size(); ++i)
			personnel->officers[i].processes =
			    initial_process_runtime({id, {}, definition(id).personnel.officers[i].id});
	if (auto *structure = ship.try_get_mut<CombatStructure>())
		for (std::size_t i = 0; i < structure->compartments.size(); ++i)
			structure->compartments[i].processes = initial_process_runtime({id, {}, {}, static_cast<std::int64_t>(i)});
}
std::string validate_process_grant(const CustomPropertyAddress &address, const ProcessGrantDefinition &grant) const
{
	(void)custom_property_address(process_actor(address));
	if (!process_runtime(address) || !process_catalog->contains(grant.process))
		throw std::runtime_error("Process grant owner or definition unavailable");
	const auto &definition = process_catalog->at(grant.process);
	static const ProcessNativeBindings none;
	const auto &bindings = this->definition(address.entity).processes.native_bindings;
	const auto native = bindings.find(address.equipment);
	if (definition.native != NativeProcessKind::Effects && address.equipment.empty())
		throw std::runtime_error("Native process requires fitted equipment owner");
	return resolve_process_native_binding(definition, native == bindings.end() ? none : native->second,
	                                      grant.native_id);
}
std::string install_process_grant(ProcessRuntime &runtime, const ProcessGrantDefinition &grant,
                                  const std::string &effect, const std::string &source, const std::string &slot,
                                  std::int64_t tick, const NativeRuleContext &origin) const
{
	validate_effect_identity(source, {}, false);
	const auto id = "temporary:" + source;
	const auto expiry = effect_expiry(tick, grant.duration_ticks);
	if (auto at = runtime.processes.find(id); at != runtime.processes.end())
	{
		if (at->second.definition != grant.process || at->second.grant_effect != effect || at->second.slot != slot ||
		    at->second.native_id != grant.native_id)
			throw std::runtime_error("Process grant identity belongs to another definition");
		at->second.expiry = refresh_effect_expiry(at->second.expiry, expiry);
		return id;
	}
	if (runtime.processes.size() >= process_owner_limit || !runtime.next_generation ||
	    runtime.next_generation >= process_generation_limit)
		throw std::runtime_error("Process grant capacity exhausted");
	ProcessState state;
	state.id = id;
	state.slot = slot;
	state.definition = grant.process;
	state.source = source;
	state.grant_effect = effect;
	state.native_id = grant.native_id;
	state.generation = runtime.next_generation++;
	state.expiry = expiry;
	state.origin = origin;
	state.requested = process_catalog->at(grant.process).requested;
	state.phase = state.requested ? "paused" : "off";
	runtime.processes.emplace(id, std::move(state));
	return id;
}
static void remove_process_grant(ProcessRuntime &runtime, const std::string &source)
{
	std::erase_if(runtime.processes,
	              [&](const auto &row)
	              {
		              return !row.second.source.empty() && row.second.source == source;
	              });
}
bool process_retirement_pending = false;
void commit_process_runtime(const CustomPropertyAddress &address, const ProcessRuntime &value)
{
	auto *runtime = process_runtime(address);
	if (!runtime)
		throw std::runtime_error("Process owner unavailable");
	for (const auto &[id, previous] : runtime->processes)
		if (previous.admitted)
		{
			const auto next = value.processes.find(id);
			if (next != value.processes.end() && previous.phase == "running" && next->second.phase != "running")
				process_retirement_pending = true;
			if (next == value.processes.end() || !next->second.admitted ||
			    next->second.generation != previous.generation || next->second.run != previous.run)
			{
				process_retirement_pending = true;
				erase_custom_property_rate_source(process_run_source(address, previous));
			}
		}
	*runtime = value;
}
void retire_process_dependants()
{
	if (!process_retirement_pending)
		return;
	process_retirement_pending = false;
	refresh_rule_eligibility();
}
bool process_origin_live(EntityId ship, const std::string &source, const PreparedEffectGroup *prepared = nullptr) const
{
	if (!entities.contains(ship))
		return false;
	for (const auto &address : custom_property_addresses(ship))
	{
		const auto *runtime = prepared && prepared->processes.contains(address) ? &prepared->processes.at(address)
		                                                                        : process_runtime(address);
		if (!runtime)
			continue;
		for (const auto &[id, state] : runtime->processes)
		{
			if (state.source == source && !state.source.empty())
				return true;
			const auto run = process_run_source(address, state);
			if (state.admitted && source == run)
				return true;
		}
	}
	return false;
}
bool process_run_available(const NativeRuleContext &context) const
{
	try
	{
		const auto address = custom_property_address(context.owner);
		const auto *runtime = process_runtime(address);
		if (!runtime)
			return false;
		for (const auto &[id, state] : runtime->processes)
			if (state.generation == context.owner_effect_id &&
			    process_run_source(address, state) == context.owner_grant)
				return state.admitted && state.requested && state.phase == "running" && state.idle_output > 0;
	}
	catch (const std::exception &)
	{
	}
	return false;
}
std::string process_exclusion_key(const CustomPropertyAddress &address,
                                  const AbilityExclusionDefinition &exclusion) const
{
	return exclusion.scope + ":" +
	       (exclusion.scope == "apparatus" ? address.equipment
	                                       : process_owner_kind(address) + ":" + process_owner_name(address)) +
	       ":" + exclusion.id;
}
std::string process_conflict(const CustomPropertyAddress &address, const ProcessState &state) const
{
	const auto &definition = process_definition(state);
	const auto ship = entities.at(address.entity);
	if (const auto *abilities = ship.try_get<AbilityRuntime>())
		for (const auto &[id, ability] : abilities->grants)
			if (ability_running(ability))
				for (const auto &wanted : definition.exclusions)
					for (const auto &held : ability_definition(address.entity, ability).exclusions)
						if (process_exclusion_key(address, wanted) == ability_exclusion_key(ability, held))
							return "Exclusive operation is busy";
	const auto native = definition.native == NativeProcessKind::Effects
	                        ? std::string{}
	                        : process_native_identity(address, state, definition);
	for (const auto &owner : custom_property_addresses(address.entity))
		if (const auto *runtime = process_runtime(owner))
			for (const auto &[id, other] : runtime->processes)
			{
				if ((owner == address && id == state.id) || !other.admitted)
					continue;
				const auto &other_definition = process_definition(other);
				if (!native.empty() && other_definition.native != NativeProcessKind::Effects &&
				    native == process_native_identity(owner, other, other_definition))
					return "Native mechanism is already in use";
				for (const auto &wanted : definition.exclusions)
					for (const auto &held : other_definition.exclusions)
						if (process_exclusion_key(address, wanted) == process_exclusion_key(owner, held))
							return "Exclusive operation is busy";
			}
	return {};
}
RuleContext process_expression_context(const CustomPropertyAddress &address, const ProcessState &state,
                                       std::optional<std::int64_t> work = {},
                                       std::optional<std::int64_t> activity = {}) const
{
	const auto &definition = process_definition(state);
	const auto context = process_context(address, state);
	RuleContext frozen;
	for (const auto &fact : definition.evaluation.facts)
		frozen.facts.push_back(fact.kind == RuleFactKind::ProcessWork
		                           ? (work ? state_tree::Value::integer(*work) : state_tree::Value{})
		                       : fact.kind == RuleFactKind::ProcessActivity
		                           ? (activity ? state_tree::Value::integer(*activity) : state_tree::Value{})
		                           : read_rule_fact(fact, context));
	return frozen;
}
std::optional<std::int64_t> process_scale(const std::optional<RuleExpression> &expression, const RuleContext &context,
                                          std::int64_t fallback = 1000) const
{
	if (!expression)
		return fallback;
	state_tree::Value value;
	if (!evaluate_rule_expression(*expression, context, value) || value.type != state_tree::Type::Integer ||
	    value.number < 0 || value.number > 1000000)
		return {};
	return value.number;
}
std::string process_ability_conflict(EntityId ship, const AbilityGrantState &ability) const
{
	const auto &definition = ability_definition(ship, ability);
	for (const auto &address : custom_property_addresses(ship))
		if (const auto *runtime = process_runtime(address))
			for (const auto &[id, state] : runtime->processes)
				if (state.admitted)
					for (const auto &wanted : definition.exclusions)
						for (const auto &held : process_definition(state).exclusions)
							if (ability_exclusion_key(ability, wanted) == process_exclusion_key(address, held))
								return "Exclusive operation is busy";
	return {};
}
void retire_ship_processes(EntityId ship)
{
	struct Pending
	{
		CustomPropertyAddress owner;
		std::string id;
	};
	std::vector<Pending> pending;
	for (const auto &address : custom_property_addresses(ship))
		if (const auto *runtime = process_runtime(address))
			for (const auto &[id, state] : runtime->processes)
				pending.push_back({address, id});
	for (const auto &item : pending)
		(void)stop_process(item.owner, item.id, true);
}
bool process_owner_terminated(EntityId id) const
{
	const auto found = entities.find(id);
	if (found == entities.end() || combat_dead(found->second) || departed(id))
		return true;
	const auto *craft = found->second.try_get<CraftState>();
	return craft && craft->phase == CraftPhase::Consumed;
}
std::string process_requirements(const CustomPropertyAddress &address, const ProcessState &state) const
{
	const auto owner = rule_entity(process_actor(address));
	if (!owner || combat_dead(owner) || process_owner_terminated(address.entity) || !process_runtime(address))
		return "Process owner unavailable";
	if (!state.source.empty() && !rule_lease_available(state.source))
		return "Process granting source unavailable";
	const auto &definition = process_definition(state);
	const auto context = process_expression_context(address, state);
	if (definition.evaluation.condition)
	{
		state_tree::Value value;
		if (!evaluate_rule_expression(*definition.evaluation.condition, context, value) ||
		    value.type != state_tree::Type::Boolean || !value.number)
			return "Process requirements unavailable";
	}
	if (!process_scale(definition.rate, context) || !process_scale(definition.upkeep_scale, context) ||
	    !process_scale(definition.work_scale, context))
		return "Invalid process arithmetic or missing fact";
	return process_native_blocker(address, state, definition);
}
std::string stop_process(const CustomPropertyAddress &address, const std::string &id, bool clear_request = true)
{
	auto *state = process_state(address, id);
	if (!state)
		return "Process unavailable";
	const auto previous = *state;
	// Stop availability before invoking its exit effects. Independent committed
	// consequences remain; dependent maintained grants see the closed run.
	state->admitted = false;
	state->phase = "off";
	state->idle_output = state->work_output = 0;
	if (clear_request)
		state->requested = false;
	if (previous.admitted)
		process_retirement_pending = true;
	state->idle_credits.clear();
	state->work_credits.clear();
	state->work_remainder = 0;
	erase_custom_property_rate_source(process_run_source(address, previous));
	const auto failure = previous.admitted
	                         ? process_execute_lifecycle(address, previous, process_definition(previous).stop_effect)
	                         : std::string{};
	if (auto *current = process_state(address, id); current && current->generation == previous.generation)
		current->blocker = failure;
	return failure;
}
bool process_request(const CustomPropertyAddress &address, const std::string &id, bool requested)
{
	auto *state = process_state(address, id);
	if (!state)
		return false;
	if (!requested)
		(void)stop_process(address, id, true);
	else
	{
		state->requested = true;
		if (!state->admitted)
			state->phase = "paused";
	}
	return true;
}
void set_process_paused(const CustomPropertyAddress &address, const std::string &id, const std::string &reason)
{
	auto *state = process_state(address, id);
	if (!state)
		return;
	const auto previous = *state;
	state->phase = state->requested ? "paused" : "off";
	state->blocker = reason;
	state->idle_output = state->work_output = 0;
	if (previous.admitted && previous.phase == "running")
	{
		process_retirement_pending = true;
		(void)process_execute_lifecycle(address, previous, process_definition(previous).pause_effect);
	}
}
void prepare_processes()
{
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	struct Pending
	{
		CustomPropertyAddress owner;
		std::string id;
	};
	std::vector<Pending> ordered;
	for (const auto &[ship, entity] : entities)
		for (const auto &address : custom_property_addresses(ship))
			if (auto *runtime = process_runtime(address))
			{
				runtime->work.clear();
				for (auto &[id, state] : runtime->processes)
				{
					state.actual_work = 0;
					ordered.push_back({address, id});
				}
			}
	for (const auto &item : ordered)
	{
		auto *state = process_state(item.owner, item.id);
		if (!state)
			continue;
		if (state->expiry >= 0 && state->expiry <= tick)
		{
			(void)stop_process(item.owner, item.id, true);
			if (auto *runtime = process_runtime(item.owner))
				runtime->processes.erase(item.id);
			continue;
		}
		if (process_owner_terminated(item.owner.entity))
		{
			if (state->admitted || state->requested)
				(void)stop_process(item.owner, item.id, true);
			continue;
		}
		if (!state->requested)
		{
			if (state->admitted)
				(void)stop_process(item.owner, item.id, true);
			continue;
		}
		const auto blocker = process_requirements(item.owner, *state);
		if (!blocker.empty())
		{
			set_process_paused(item.owner, item.id, blocker);
			continue;
		}
		const auto &definition = process_definition(*state);
		const auto frozen = process_expression_context(item.owner, *state);
		state->rate_permille = *process_scale(definition.rate, frozen);
		if (!state->admitted)
		{
			const auto conflict = process_conflict(item.owner, *state);
			if (!conflict.empty())
			{
				set_process_paused(item.owner, item.id, conflict);
				continue;
			}
			auto *runtime = process_runtime(item.owner);
			if (runtime->next_run >= 9007199254740991ULL)
			{
				set_process_paused(item.owner, item.id, "Process run identity exhausted");
				continue;
			}
			auto candidate = *state;
			candidate.run = runtime->next_run;
			std::vector<EffectGroupDefinition> groups;
			if (!definition.entry_costs.empty())
			{
				EffectGroupDefinition entry;
				entry.costs = definition.entry_costs;
				groups.push_back(std::move(entry));
			}
			if (definition.start_effect)
				groups.push_back(*definition.start_effect);
			std::vector<EffectGroupInvocation> frames;
			for (const auto &group : groups)
				frames.push_back(process_effect_invocation(item.owner, candidate, group));
			auto quote = prepare_effect_groups(frames);
			quote.original_processes.try_emplace(item.owner, *runtime);
			auto &staged = quote.processes.try_emplace(item.owner, *runtime).first->second;
			if (auto at = staged.processes.find(item.id); at != staged.processes.end())
			{
				if (at->second.generation != candidate.generation || staged.next_run != candidate.run)
					quote.failure = "Process admission ownership changed";
				else
				{
					at->second.run = staged.next_run++;
					at->second.started = tick;
					at->second.admitted = true;
					at->second.phase = "paused";
				}
			}
			const auto failure = commit_effect_group(quote);
			state = process_state(item.owner, item.id);
			if (!state || state->generation != candidate.generation)
				continue;
			if (!failure.empty())
			{
				set_process_paused(item.owner, item.id, failure);
				continue;
			}
		}
		state->blocker.clear();
		// The existing allocation phase decides whether this run can operate. No
		// pause/resume or work effects are published from a provisional quote here.
		stage_process_upkeep(item.owner, *state, definition);
	}
}
std::string stage_process_funding_state(const CustomPropertyAddress &address, ProcessState &state, std::int64_t idle,
                                        std::int64_t activity) const
{
	if (idle < 0 || idle > 1000 || activity < 0 || activity > 1000)
		throw std::runtime_error("Invalid process funding activity");
	if (!state.admitted || !state.requested)
		return {};
	const auto &definition = process_definition(state);
	if (idle <= 0 || (definition.shortage != "proportional" && (idle < 1000 || activity < 1000)))
	{
		state.idle_output = state.work_output = 0;
		state.blocker = "Insufficient process funding";
		if (definition.shortage == "stop")
		{
			state.admitted = false;
			state.requested = false;
			state.phase = "off";
			state.idle_credits.clear();
			state.work_credits.clear();
			state.work_remainder = 0;
			return "stop";
		}
		const bool pausing = state.phase == "running";
		state.phase = "paused";
		return pausing ? "pause" : std::string{};
	}
	const bool resuming = state.phase == "paused" && state.started < static_cast<std::int64_t>(ecs.get<Clock>().tick);
	state.idle_output = idle;
	state.work_output = activity;
	state.phase = "running";
	state.blocker.clear();
	return resuming ? "resume" : std::string{};
}
std::optional<EffectGroupDefinition> process_funding_effect(const CustomPropertyAddress &address,
                                                            const ProcessState &state, std::int64_t idle,
                                                            std::int64_t activity) const
{
	auto candidate = state;
	const auto transition = stage_process_funding_state(address, candidate, idle, activity);
	const auto &definition = process_definition(state);
	return transition == "pause"    ? definition.pause_effect
	       : transition == "resume" ? definition.resume_effect
	       : transition == "stop"   ? definition.stop_effect
	                                : std::optional<EffectGroupDefinition>{};
}
std::int64_t process_effect_work(const CustomPropertyAddress &address, const ProcessState &state,
                                 std::int64_t work) const
{
	const auto divisor = process_definition(state).work_divisor;
	if (work < 0 || work > resource_limit || state.work_remainder < 0 || state.work_remainder >= divisor)
		throw std::runtime_error("Invalid process fractional work");
	const auto total = work + state.work_remainder;
	return total / divisor * divisor;
}
std::int64_t process_next_work_remainder(const CustomPropertyAddress &address, const ProcessState &state,
                                         std::int64_t work) const
{
	return work + state.work_remainder - process_effect_work(address, state, work);
}
RuleEvaluation process_work_effects(const CustomPropertyAddress &address, const ProcessState &state, std::int64_t work,
                                    std::int64_t activity) const
{
	const auto &definition = process_definition(state);
	const auto whole = process_effect_work(address, state, work);
	if (!whole)
	{
		RuleEvaluation empty;
		empty.status = RuleEvaluationStatus::Ready;
		return empty;
	}
	return evaluate_rule(definition.evaluation, process_expression_context(address, state, whole, activity));
}
void commit_process_work_receipt(const CustomPropertyAddress &address, const ProcessState &frozen, std::int64_t work,
                                 std::int64_t activity)
{
	auto *runtime = process_runtime(address);
	if (!runtime || !frozen.admitted || !frozen.run || work < 0 || work > resource_limit || activity < 0 ||
	    activity > 1000)
		throw std::runtime_error("Invalid completed process work");
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	auto *state = process_state(address, frozen.id);
	const bool same = state && state->generation == frozen.generation && state->run == frozen.run;
	if ((same && state->last_work == tick) || std::any_of(runtime->work.begin(), runtime->work.end(),
	                                                      [&](const auto &receipt)
	                                                      {
		                                                      return receipt.generation == frozen.generation &&
		                                                             receipt.activation == frozen.run;
	                                                      }))
		throw std::runtime_error("Process work already committed this tick");
	if (same)
	{
		state->last_work = tick;
		state->actual_work = work;
		if (state->admitted && state->phase == "running")
			state->work_output = activity;
	}
	if (!work || !activity)
		return;
	const auto &definition = process_definition(frozen);
	runtime->work.push_back({process_owner_kind(address), process_owner_name(address), definition.family, definition.id,
	                         frozen.run, activity, frozen.id, work, definition.work_unit, frozen.generation});
}
void publish_process_work()
{
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	for (const auto &[ship, entity] : entities)
		for (const auto &address : custom_property_addresses(ship))
			if (auto *runtime = process_runtime(address))
				runtime->work_tick = tick;
}
std::optional<std::int64_t> process_completed_work(const RuleActor &actor, const std::string &family) const
{
	if (!entities.contains(actor.entity))
		return {};
	std::int64_t output = 0;
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	for (const auto &address : custom_property_addresses(actor.entity))
	{
		if ((!actor.equipment.empty() && address.equipment != actor.equipment) ||
		    (!actor.officer.empty() && address.officer != actor.officer) ||
		    (actor.compartment >= 0 && address.compartment != actor.compartment))
			continue;
		const auto *runtime = process_runtime(address);
		if (!runtime || runtime->work_tick != tick)
			return {};
		for (const auto &receipt : runtime->work)
			if (family.empty() || receipt.family == family)
				output += receipt.output;
	}
	return output;
}
Json save_processes() const
{
	Json saved = Json::array();
	for (const auto &[ship, entity] : entities)
		if (ship < field_identity_base)
			for (const auto &address : custom_property_addresses(ship))
				if (const auto *runtime = process_runtime(address);
				    runtime && (!runtime->processes.empty() || runtime->next_generation != 1 ||
				                runtime->next_run != 1 || !runtime->work.empty()))
					saved.push_back({{"entity", address.entity},
					                 {"equipment", address.equipment},
					                 {"officer", address.officer},
					                 {"compartment", address.compartment},
					                 {"state", *runtime}});
	return saved;
}
void validate_process_runtime(const CustomPropertyAddress &address, const ProcessRuntime &runtime) const
{
	constexpr auto maximum = 9007199254740991ULL;
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	if (!runtime.next_generation || runtime.next_generation > process_generation_limit || !runtime.next_run ||
	    runtime.next_run > maximum || runtime.processes.size() > process_owner_limit ||
	    runtime.work.size() > process_owner_limit || runtime.work_tick < -1 || runtime.work_tick > tick)
		throw std::runtime_error("Invalid process runtime bounds");
	const auto initial = initial_process_runtime(address);
	std::set<std::uint64_t> generations, runs;
	for (const auto &[id, state] : runtime.processes)
	{
		if (id != state.id || id.empty() || id.size() > 1024 || !valid_identifier(state.slot) ||
		    !process_catalog->contains(state.definition) || !state.generation ||
		    state.generation >= runtime.next_generation || !generations.insert(state.generation).second ||
		    state.run >= runtime.next_run || state.started < -1 || state.started > tick || state.last_work < -1 ||
		    state.last_work > tick || state.blocker.size() > 1024 || state.idle_output < 0 ||
		    state.idle_output > 1000 || state.work_output < 0 || state.work_output > 1000 || state.actual_work < 0 ||
		    state.actual_work > resource_limit || state.rate_permille < 0 || state.rate_permille > 1000000 ||
		    state.work_remainder < 0 || state.work_remainder >= process_catalog->at(state.definition).work_divisor)
			throw std::runtime_error("Invalid process execution state");
		if (state.phase == "running" ? state.idle_output <= 0 : state.idle_output || state.work_output)
			throw std::runtime_error("Contradictory process activity");
		if (state.phase != "off" && state.phase != "running" && state.phase != "paused")
			throw std::runtime_error("Invalid process phase");
		if (state.admitted
		        ? (!state.requested || !state.run || state.phase == "off" || state.started < 0 ||
		           !runs.insert(state.run).second)
		        : (state.phase == "running" || (state.requested ? state.phase != "paused" : state.phase != "off")))
			throw std::runtime_error("Contradictory process lifecycle");
		if (!state.admitted && (!state.idle_credits.empty() || !state.work_credits.empty() || state.work_remainder))
			throw std::runtime_error("Unadmitted process retained paid work");
		validate_saved_rule_context(Json(state.origin));
		if (state.source.empty())
		{
			const auto expected = initial.processes.find(id);
			if (expected == initial.processes.end() || state.definition != expected->second.definition ||
			    state.slot != expected->second.slot || state.native_id != expected->second.native_id ||
			    !state.grant_effect.empty() || state.expiry != -1)
				throw std::runtime_error("Saved static process declaration changed");
		}
		else
		{
			const auto grant = process_grant_templates.find(state.grant_effect);
			if (grant == process_grant_templates.end() || grant->second.process != state.definition ||
			    validate_process_grant(address, grant->second) != state.native_id ||
			    id != "temporary:" + state.source || state.source.size() > 512 ||
			    (grant->second.duration_ticks == 0
			         ? state.expiry != -1
			         : state.expiry < 0 || state.expiry > tick + grant->second.duration_ticks))
				throw std::runtime_error("Invalid temporary process lineage/lifetime");
		}
		for (const auto *credits : {&state.idle_credits, &state.work_credits})
		{
			if (credits->size() > 4096)
				throw std::runtime_error("Process work credit capacity exhausted");
			std::set<
			    std::tuple<EntityId, std::string, std::string, std::string, std::string, std::string, std::int64_t>>
			    distinct;
			for (const auto &credit : *credits)
			{
				validate_saved_work_credit(Json(credit));
				const auto denomination = credits == &state.idle_credits
				                              ? resource_work_unit * 1000
				                              : process_catalog->at(state.definition).work_divisor * 1000000;
				if (credit.unit_work != denomination)
					throw std::runtime_error("Saved process credit has wrong work denomination");
				if (!distinct
				         .emplace(credit.payer, credit.store, credit.resource, credit.property, credit.equipment,
				                  credit.officer, credit.compartment)
				         .second)
					throw std::runtime_error("Duplicate process work credit");
			}
		}
	}
	for (const auto &[id, state] : initial.processes)
		if (!runtime.processes.contains(id))
			throw std::runtime_error("Missing static process declaration");
	std::set<std::pair<std::uint64_t, std::uint64_t>> receipts;
	for (const auto &receipt : runtime.work)
	{
		const auto found = process_catalog->find(receipt.definition);
		if (found == process_catalog->end() || found->second.family != receipt.family ||
		    found->second.work_unit != receipt.work_unit || receipt.source != process_owner_kind(address) ||
		    receipt.owner != process_owner_name(address) || receipt.execution.empty() ||
		    receipt.execution.size() > 1024 || !receipt.activation || receipt.activation >= runtime.next_run ||
		    !receipt.generation || receipt.generation >= runtime.next_generation || receipt.work <= 0 ||
		    receipt.work > resource_limit || receipt.output <= 0 || receipt.output > 1000 ||
		    !receipts.emplace(receipt.generation, receipt.activation).second)
			throw std::runtime_error("Invalid completed process work receipt");
	}
}
void validate_saved_process_state(const Json &state) const
{
	rule_saved_fields(state, {"processes", "next_generation", "next_run", "work_tick", "work"});
	rule_saved_integer(state.at("next_generation"), 1);
	rule_saved_integer(state.at("next_run"), 1);
	rule_saved_integer(state.at("work_tick"), -1);
	if (!state.at("processes").is_object() || state.at("processes").size() > process_owner_limit ||
	    !state.at("work").is_array() || state.at("work").size() > process_owner_limit)
		throw std::runtime_error("Invalid process roster shape");
	for (const auto &[id, process] : state.at("processes").items())
	{
		rule_saved_fields(
		    process, {"id",          "slot",        "definition",  "source",        "grant_effect", "native_id",
		              "generation",  "run",         "expiry",      "started",       "last_work",    "requested",
		              "admitted",    "origin",      "phase",       "idle_credits",  "work_credits", "work_remainder",
		              "idle_output", "work_output", "actual_work", "rate_permille", "blocker"});
		for (const auto name : {"id", "slot", "definition", "source", "grant_effect", "native_id", "phase", "blocker"})
			rule_saved_text(process.at(name), 1024);
		for (const auto name :
		     {"generation", "run", "work_remainder", "idle_output", "work_output", "actual_work", "rate_permille"})
			rule_saved_integer(process.at(name));
		for (const auto name : {"expiry", "started", "last_work"})
			rule_saved_integer(process.at(name), -1);
		rule_saved_bool(process.at("requested"));
		rule_saved_bool(process.at("admitted"));
		validate_saved_rule_context(process.at("origin"));
		for (const auto name : {"idle_credits", "work_credits"})
		{
			if (!process.at(name).is_array() || process.at(name).size() > 4096)
				throw std::runtime_error("Invalid process credit roster");
			for (const auto &credit : process.at(name))
				validate_saved_work_credit(credit);
		}
	}
	for (const auto &receipt : state.at("work"))
	{
		rule_saved_fields(receipt, {"source", "owner", "family", "definition", "activation", "output", "execution",
		                            "work", "work_unit", "generation"});
		for (const auto name : {"source", "owner", "family", "definition", "execution", "work_unit"})
			rule_saved_text(receipt.at(name), 1024);
		for (const auto name : {"activation", "output", "work", "generation"})
			rule_saved_integer(receipt.at(name));
	}
}
void restore_processes(const Json &saved, const std::set<EntityId> &departed_ships)
{
	if (!saved.is_array())
		throw std::runtime_error("Invalid process owner roster");
	std::map<CustomPropertyAddress, ProcessRuntime> staged;
	for (const auto &row : saved)
	{
		if (!row.is_object() || row.size() != 5)
			throw std::runtime_error("Invalid process owner row");
		CustomPropertyAddress address{
		    static_cast<EntityId>(checked_integer(row.at("entity"), 1, 9007199254740991LL, "process.entity")),
		    row.at("equipment").get<std::string>(), row.at("officer").get<std::string>(),
		    checked_integer(row.at("compartment"), -1, 255, "process.compartment")};
		if ((!address.equipment.empty() && (!address.officer.empty() || address.compartment >= 0)) ||
		    (!address.officer.empty() && address.compartment >= 0) || !process_runtime(address) ||
		    staged.contains(address))
			throw std::runtime_error("Invalid native process owner identity");
		const auto &state = row.at("state");
		if (!state.is_object() || state.size() != 5 || !state.at("processes").is_object() ||
		    state.at("processes").size() > process_owner_limit || !state.at("work").is_array() ||
		    state.at("work").size() > process_owner_limit)
			throw std::runtime_error("Invalid saved process state shape");
		validate_saved_process_state(state);
		auto runtime = state.get<ProcessRuntime>();
		validate_process_runtime(address, runtime);
		if (departed_ships.contains(address.entity))
			for (const auto &[id, process] : runtime.processes)
				if (process.admitted || process.requested)
					throw std::runtime_error("Departed owner retained active process");
		staged.emplace(address, std::move(runtime));
	}
	for (const auto &[ship, entity] : entities)
		for (const auto &address : custom_property_addresses(ship))
		{
			if (!process_owner_bindings(address).empty() && !staged.contains(address))
				throw std::runtime_error("Missing process owner state");
			if (auto *runtime = process_runtime(address))
				*runtime = staged.contains(address) ? std::move(staged.at(address)) : initial_process_runtime(address);
		}
	process_retirement_pending = false;
}
