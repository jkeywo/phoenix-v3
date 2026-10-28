module;
#include "native_headers.hpp"

module phoenix.simulation;
import :effect_work;

namespace phoenix::world_detail
{
auto Runtime::process_runtime(const CustomPropertyAddress &address) const -> ProcessRuntime *
{
	const auto at = entities.find(address.entity);
	return at == entities.end()
	           ? nullptr
	           : phoenix::process_runtime(at->second, address, native_owner_definitions(address.entity).personnel);
}

auto Runtime::process_state(const CustomPropertyAddress &address, const std::string &id) const -> ProcessState *
{
	auto *runtime = process_runtime(address);
	if (!runtime)
		return nullptr;
	const auto at = runtime->processes.find(id);
	return at == runtime->processes.end() ? nullptr : &at->second;
}

auto Runtime::process_definition(const ProcessState &state) const -> const ProcessDefinition &
{
	return process_catalog->at(state.definition);
}

auto Runtime::process_context(const CustomPropertyAddress &address, const ProcessState &state) const
    -> NativeRuleContext
{
	auto context = state.origin;
	const auto owner = process_actor(address);
	context.owner = context.source = context.subject = context.target = owner;
	if (!state.target.empty() && state.origin.target.entity &&
	    process_definition(state).native != NativeProcessKind::Effects)
		context.subject = context.target = state.origin.target;
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

auto Runtime::process_effect_invocation(const CustomPropertyAddress &address, const ProcessState &state,
                                        const EffectGroupDefinition &group, std::int64_t output) const
    -> EffectGroupInvocation
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

auto Runtime::process_execute_groups(const CustomPropertyAddress &address, const ProcessState &state,
                                     std::span<const EffectGroupDefinition> groups, std::int64_t output) -> std::string
{
	std::vector<EffectGroupInvocation> frames;
	for (const auto &group : groups)
		frames.push_back(process_effect_invocation(address, state, group, output));
	if (frames.empty())
		return {};
	auto quote = prepare_effect_groups(frames);
	return commit_effect_group(quote);
}

auto Runtime::process_execute_lifecycle(const CustomPropertyAddress &address, const ProcessState &state,
                                        const std::optional<EffectGroupDefinition> &group) -> std::string
{
	if (!group)
		return {};
	return process_execute_groups(address, state, std::span<const EffectGroupDefinition>(&*group, 1));
}

auto Runtime::process_owner_bindings(const CustomPropertyAddress &address) const -> const std::vector<ProcessBinding> &
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

auto Runtime::initial_process_runtime(const CustomPropertyAddress &address) const -> ProcessRuntime
{
	return phoenix::initial_process_runtime(process_owner_bindings(address), *process_catalog);
}

auto Runtime::initialise_processes(flecs::entity ship) -> void
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

auto Runtime::validate_process_grant(const CustomPropertyAddress &address, const ProcessGrantDefinition &grant) const
    -> std::string
{
	(void)custom_property_address(process_actor(address));
	if (!process_runtime(address) || !process_catalog->contains(grant.process))
		throw std::runtime_error("Process grant owner or definition unavailable");
	const auto &definition = process_catalog->at(grant.process);
	static const ProcessNativeBindings none;
	const auto &bindings = this->definition(address.entity).processes.native_bindings;
	const auto native = bindings.find(address.equipment);
	if (definition.native != NativeProcessKind::Effects && address.equipment.empty() &&
	    !(definition.native == NativeProcessKind::Recovery && address.officer.empty() && address.compartment < 0))
		throw std::runtime_error("Native process requires fitted equipment owner");
	return resolve_process_native_binding(definition, native == bindings.end() ? none : native->second,
	                                      grant.native_id);
}

auto Runtime::install_process_grant(ProcessRuntime &runtime, const ProcessGrantDefinition &grant,
                                    const std::string &effect, const std::string &source, const std::string &slot,
                                    std::int64_t tick, const NativeRuleContext &origin) const -> std::string
{
	return phoenix::install_process_grant(runtime, *process_catalog, grant, effect, source, slot, tick, origin);
}

auto Runtime::commit_process_runtime(const CustomPropertyAddress &address, const ProcessRuntime &value) -> void
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

auto Runtime::retire_process_dependants() -> void
{
	if (!process_retirement_pending)
		return;
	process_retirement_pending = false;
	refresh_rule_eligibility();
}

auto Runtime::process_origin_live(EntityId ship, const std::string &source, const PreparedEffectGroup *prepared) const
    -> bool
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

auto Runtime::process_run_available(const NativeRuleContext &context) const -> bool
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

auto Runtime::process_exclusion_key(const CustomPropertyAddress &address,
                                    const AbilityExclusionDefinition &exclusion) const -> std::string
{
	return exclusion.scope + ":" +
	       (exclusion.scope == "apparatus" ? address.equipment
	                                       : process_owner_kind(address) + ":" + process_owner_name(address)) +
	       ":" + exclusion.id;
}

auto Runtime::process_conflict(const CustomPropertyAddress &address, const ProcessState &state) const -> std::string
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
	const auto &driver = state.parent_process.empty() ? state.id : state.parent_process;
	const auto native = definition.native == NativeProcessKind::Effects
	                        ? std::string{}
	                        : process_native_identity(address, state, definition);
	for (const auto &owner : custom_property_addresses(address.entity))
		if (const auto *runtime = process_runtime(owner))
			for (const auto &[id, other] : runtime->processes)
			{
				if ((owner == address && id == state.id) || !other.admitted)
					continue;
				if (owner == address && !other.parent_process.empty() && other.parent_process == driver)
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

auto Runtime::process_expression_context(const ProcessDefinition &definition, const NativeRuleContext &context,
                                         std::optional<std::int64_t> work, std::optional<std::int64_t> activity) const
    -> RuleContext
{
	RuleContext frozen;
	for (const auto &fact : definition.evaluation.facts)
		frozen.facts.push_back(fact.kind == RuleFactKind::ProcessWork
		                           ? (work ? state_tree::Value::integer(*work) : state_tree::Value{})
		                       : fact.kind == RuleFactKind::ProcessActivity
		                           ? (activity ? state_tree::Value::integer(*activity) : state_tree::Value{})
		                           : read_rule_fact(fact, context));
	return frozen;
}

auto Runtime::process_expression_context(const CustomPropertyAddress &address, const ProcessState &state,
                                         std::optional<std::int64_t> work, std::optional<std::int64_t> activity) const
    -> RuleContext
{
	return process_expression_context(process_definition(state), process_context(address, state), work, activity);
}

auto Runtime::process_ability_conflict(EntityId ship, const AbilityGrantState &ability) const -> std::string
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

auto Runtime::retire_ship_processes(EntityId ship) -> void
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

auto Runtime::process_owner_terminated(EntityId id) const -> bool
{
	const auto found = entities.find(id);
	if (found == entities.end() || combat_dead(found->second) || departed(id))
		return true;
	const auto *craft = found->second.try_get<CraftState>();
	return craft && craft->phase == CraftPhase::Consumed;
}

auto Runtime::process_eligibility(const CustomPropertyAddress &address, const ProcessState &state,
                                  const RuleActor *target) const -> std::string
{
	const auto owner = rule_entity(process_actor(address));
	if (!owner || combat_dead(owner) || process_owner_terminated(address.entity) || !process_runtime(address))
		return "Process owner unavailable";
	if (!state.source.empty() && !rule_lease_available(state.source))
		return "Process granting source unavailable";
	const auto &definition = process_definition(state);
	auto native_context = process_context(address, state);
	if (target)
		native_context.subject = native_context.target = *target;
	const auto context = process_expression_context(definition, native_context);
	if (const auto blocker = evaluate_process_requirements(definition, context); !blocker.empty())
		return blocker;
	return process_native_blocker(address, state, definition);
}

auto Runtime::process_requirements(const CustomPropertyAddress &address, const ProcessState &state) const -> std::string
{
	if (!state.parent_process.empty())
	{
		const auto *driver = process_state(address, state.parent_process);
		if (!driver || !driver->requested || !driver->parent_process.empty() || driver->generation >= state.generation)
			return "Process driver unavailable";
	}
	return process_eligibility(address, state);
}

auto Runtime::reset_process_run_state(ProcessState &state, bool clear_request) const -> void
{
	phoenix::reset_process_run_state(state, process_definition(state), clear_request);
}

auto Runtime::stop_process(const CustomPropertyAddress &address, const std::string &id, bool clear_request)
    -> std::string
{
	auto *state = process_state(address, id);
	if (!state)
		return "Process unavailable";
	const auto previous = *state;
	if (state->parent_process.empty())
	{
		std::vector<std::string> children;
		for (const auto &[child_id, child] : process_runtime(address)->processes)
			if (child.parent_process == id)
				children.push_back(child_id);
		for (const auto &child : children)
			(void)stop_process(address, child, true);
		state = process_state(address, id);
		if (!state)
			return {};
	}
	// Stop availability before invoking its exit effects. Independent committed
	// consequences remain; dependent maintained grants see the closed run.
	reset_process_run_state(*state, clear_request);
	if (previous.admitted)
		process_retirement_pending = true;
	erase_custom_property_rate_source(process_run_source(address, previous));
	const auto failure = previous.admitted
	                         ? process_execute_lifecycle(address, previous, process_definition(previous).stop_effect)
	                         : std::string{};
	if (auto *current = process_state(address, id); current && current->generation == previous.generation)
		current->blocker = failure;
	return failure;
}

auto Runtime::process_request(const CustomPropertyAddress &address, const std::string &id, bool requested) -> bool
{
	auto *state = process_state(address, id);
	if (!state)
		return false;
	if (!requested)
		(void)stop_process(address, id, true);
	else
	{
		if (state->parent_process.empty())
			std::erase_if(process_runtime(address)->processes,
			              [&](const auto &child)
			              {
				              return child.second.parent_process == id && !child.second.admitted &&
				                     !child.second.requested;
			              });
		if (state->completed)
		{
			// A new finite episode retires the old funding/lifetime source just
			// like an explicit stop; physical native progress remains native.
			(void)stop_process(address, id, false);
			state = process_state(address, id);
			if (!state)
				return false;
		}
		state->requested = true;
		if (!state->admitted)
			state->phase = "paused";
	}
	return true;
}

auto Runtime::set_process_paused(const CustomPropertyAddress &address, const std::string &id, const std::string &reason)
    -> void
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

auto Runtime::admit_process_run(const CustomPropertyAddress &address, const std::string &id) -> bool
{
	auto *state = process_state(address, id);
	if (!state || !state->requested)
		return false;
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	const auto &definition = process_definition(*state);
	if (!state->admitted)
	{
		const auto conflict = process_conflict(address, *state);
		if (!conflict.empty())
		{
			set_process_paused(address, id, conflict);
			return false;
		}
		auto *runtime = process_runtime(address);
		if (runtime->next_run >= 9007199254740991ULL)
		{
			set_process_paused(address, id, "Process run identity exhausted");
			return false;
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
			frames.push_back(process_effect_invocation(address, candidate, group));
		auto quote = prepare_effect_groups(frames);
		quote.original_processes.try_emplace(address, *runtime);
		auto &staged = quote.processes.try_emplace(address, *runtime).first->second;
		if (auto at = staged.processes.find(id); at != staged.processes.end())
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
		state = process_state(address, id);
		if (!state || state->generation != candidate.generation)
			return false;
		if (!failure.empty())
		{
			set_process_paused(address, id, failure);
			return false;
		}
	}
	return true;
}

auto Runtime::begin_finite_native_run(const CustomPropertyAddress &address, const std::string &id,
                                      const std::string &target, const RuleActor &target_actor, std::int64_t progress,
                                      std::int64_t required) -> std::string
{
	auto *driver = process_state(address, id);
	if (!driver || !driver->parent_process.empty() || !driver->requested || target.empty() || target.size() > 1024 ||
	    progress < 0 || required < 0 || progress > resource_limit || required > resource_limit)
		return {};
	const auto blocker = process_native_blocker(address, *driver, process_definition(*driver));
	if (process_owner_terminated(address.entity) || (!driver->source.empty() && !rule_lease_available(driver->source)))
		return {};
	if (!blocker.empty())
	{
		set_process_paused(address, id, blocker);
		return {};
	}
	const auto target_hash =
	    sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(target.data()), target.size()));
	const auto episode = "episode:" + std::to_string(driver->generation) + ":" + target_hash;
	auto *runtime = process_runtime(address);
	auto found = runtime->processes.find(episode);
	if (found != runtime->processes.end() && !found->second.requested)
		return {};
	if (found != runtime->processes.end() && found->second.completed)
	{
		(void)stop_process(address, episode, false);
		runtime = process_runtime(address);
		driver = process_state(address, id);
		if (!runtime || !driver || !driver->requested)
			return {};
		runtime->processes.erase(episode);
		found = runtime->processes.end();
	}
	if (found == runtime->processes.end())
	{
		// Completed target snapshots may be retired to admit new work. Independent
		// consequences retain their existing application identities and lifetimes.
		if (runtime->processes.size() >= process_owner_limit)
		{
			for (auto it = runtime->processes.begin();
			     it != runtime->processes.end() && runtime->processes.size() >= process_owner_limit;)
				if (!it->second.parent_process.empty() && !it->second.admitted && it->second.completed)
					it = runtime->processes.erase(it);
				else
					++it;
		}
		if (runtime->processes.size() >= process_owner_limit || runtime->next_generation >= process_generation_limit)
		{
			set_process_paused(address, id, "Process target capacity exhausted");
			return {};
		}
		ProcessState candidate;
		candidate.id = episode;
		candidate.parent_process = id;
		candidate.slot = driver->slot;
		candidate.definition = driver->definition;
		candidate.native_id = driver->native_id;
		candidate.source = driver->source;
		candidate.grant_effect = driver->grant_effect;
		candidate.expiry = driver->expiry;
		candidate.origin = driver->origin;
		candidate.generation = runtime->next_generation++;
		candidate.requested = true;
		candidate.phase = "paused";
		candidate.rate_permille = driver->rate_permille;
		runtime->processes.emplace(episode, std::move(candidate));
	}
	auto *state = process_state(address, episode);
	state->target = target;
	state->origin.target = target_actor;
	state->progress = progress;
	state->required_work = required;
	state->rate_permille = driver->rate_permille;
	const auto requirements = process_requirements(address, *state);
	if (!requirements.empty())
	{
		set_process_paused(address, episode, requirements);
		return {};
	}
	const auto rate_scale = process_scale(process_definition(*state).rate, process_expression_context(address, *state));
	if (!rate_scale)
		return {};
	state->rate_permille = *rate_scale;
	if (!admit_process_run(address, episode))
		return {};
	return episode;
}

auto Runtime::prepare_processes() -> void
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
		if (state->completed && process_definition(*state).native != NativeProcessKind::Ftl)
			continue;
		const auto blocker = process_requirements(item.owner, *state);
		if (!blocker.empty())
		{
			set_process_paused(item.owner, item.id, blocker);
			continue;
		}
		const auto &definition = process_definition(*state);
		const auto frozen = process_expression_context(item.owner, *state);
		state->rate_permille = *process_scale(definition.rate, frozen);
		const bool late_native = definition.native == NativeProcessKind::Repair ||
		                         definition.native == NativeProcessKind::Medical ||
		                         definition.native == NativeProcessKind::Recovery ||
		                         definition.native == NativeProcessKind::ServicingEnergy ||
		                         definition.native == NativeProcessKind::ServicingAmmunition;
		if (late_native)
			continue;
		if (!admit_process_run(item.owner, item.id))
			continue;
		state = process_state(item.owner, item.id);
		state->blocker.clear();
		// The existing allocation phase decides whether this run can operate. No
		// pause/resume or work effects are published from a provisional quote here.
		stage_process_upkeep(item.owner, *state, definition);
	}
}

auto Runtime::stage_process_funding_state(const CustomPropertyAddress &, ProcessState &state, std::int64_t idle,
                                          std::int64_t activity) const -> std::string
{
	return phoenix::stage_process_funding_state(state, process_definition(state),
	                                            static_cast<std::int64_t>(ecs.get<Clock>().tick), idle, activity);
}

auto Runtime::process_funding_effect(const CustomPropertyAddress &, const ProcessState &state, std::int64_t idle,
                                     std::int64_t activity) const -> std::optional<EffectGroupDefinition>
{
	return phoenix::process_funding_effect(process_definition(state), state,
	                                       static_cast<std::int64_t>(ecs.get<Clock>().tick), idle, activity);
}

auto Runtime::process_effect_work(const CustomPropertyAddress &, const ProcessState &state, std::int64_t work) const
    -> std::int64_t
{
	return phoenix::process_effect_work(process_definition(state), state, work);
}

auto Runtime::process_next_work_remainder(const CustomPropertyAddress &, const ProcessState &state,
                                          std::int64_t work) const -> std::int64_t
{
	return phoenix::process_next_work_remainder(process_definition(state), state, work);
}

auto Runtime::process_work_effects(const CustomPropertyAddress &address, const ProcessState &state, std::int64_t work,
                                   std::int64_t activity) const -> RuleEvaluation
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

auto Runtime::commit_finite_process_completion(const CustomPropertyAddress &address, const std::string &id,
                                               const std::string &target) -> void
{
	auto *state = process_state(address, id);
	if (!state || state->completed || !state->admitted)
		return;
	const auto frozen = *state;
	const auto &definition = process_definition(frozen);
	record_process_completion(*state, definition, target);
	if (definition.native != NativeProcessKind::Ftl)
	{
		process_retirement_pending = true;
		erase_custom_property_rate_source(process_run_source(address, frozen));
	}
	const auto failure = process_execute_lifecycle(address, frozen, definition.completion_effect);
	if (!failure.empty())
	{
		if (auto *current = process_state(address, id);
		    current && current->generation == frozen.generation && current->run == frozen.run)
			current->blocker = "Completion effect failed";
		rule_events.record(rule_events.claim_identity(), "failed: process completion " + failure.substr(0, 900));
	}
}

auto Runtime::commit_process_work_receipt(const CustomPropertyAddress &address, const ProcessState &frozen,
                                          std::int64_t work, std::int64_t activity) -> void
{
	auto *runtime = process_runtime(address);
	if (!runtime)
		throw std::runtime_error("Invalid completed process work");
	if (record_process_work(*runtime, address, process_definition(frozen), frozen,
	                        static_cast<std::int64_t>(ecs.get<Clock>().tick), work, activity))
		commit_finite_process_completion(address, frozen.id, {});
}

auto Runtime::publish_process_work() -> void
{
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	for (const auto &[ship, entity] : entities)
		for (const auto &address : custom_property_addresses(ship))
			if (auto *runtime = process_runtime(address))
				runtime->work_tick = tick;
}
} // namespace phoenix::world_detail
