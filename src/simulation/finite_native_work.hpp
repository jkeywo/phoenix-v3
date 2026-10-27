// Included inside World::Impl after rule_funding.hpp. Late native jobs use the
// same prepared transactions and fractional input credits as continuous work.
struct FiniteNativeWork
{
	CustomPropertyAddress owner;
	ProcessState frozen;
	std::int64_t work = 0;
	PreparedEffectGroup prepared;
	std::vector<RuleUpkeepPlan> payments;
	bool paid = false;
};
NativeProcessView finite_native_driver(EntityId entity, std::string_view kind, const std::string &native,
                                       const RuleActor *target = nullptr) const
{
	std::vector<std::pair<bool, NativeProcessView>> candidates;
	for (const auto &address : custom_property_addresses(entity))
		if (const auto *runtime = process_runtime(address))
			for (const auto &[id, state] : runtime->processes)
			{
				const auto &definition = process_definition(state);
				if (!state.parent_process.empty() || native_process_kind_name(definition.native) != kind ||
				    state.native_id != native || !state.requested)
					continue;
				if (!process_native_blocker(address, state, definition).empty() ||
				    (!state.source.empty() && !rule_lease_available(state.source)))
					continue;
				const auto active = std::any_of(runtime->processes.begin(), runtime->processes.end(),
				                                [&](const auto &entry)
				                                {
					                                return entry.second.parent_process == id && entry.second.admitted;
				                                });
				candidates.push_back({active, {address, &state}});
			}
	// Preserve the admitted mechanism owner; otherwise use stable declaration order.
	std::stable_sort(candidates.begin(), candidates.end(),
	                 [](const auto &a, const auto &b)
	                 {
		                 return a.first > b.first;
	                 });
	for (const auto &[active, candidate] : candidates)
	{
		if (target)
		{
			auto probe = *candidate.state;
			probe.parent_process = probe.id;
			probe.id = "candidate:" + probe.id;
			probe.generation = process_runtime(candidate.owner)->next_generation;
			probe.target = "native-work";
			probe.origin.target = *target;
			if (!process_requirements(candidate.owner, probe).empty() ||
			    !process_conflict(candidate.owner, probe).empty())
				continue;
		}
		return candidate;
	}
	return {};
}

std::int64_t stage_finite_native_costs(FiniteNativeWork &quote, RuleUpkeepPlan plan, bool proportional)
{
	resolve_rule_upkeep("finite." + std::to_string(quote.owner.entity), plan);
	std::vector<RuleWorkInput> inputs;
	for (const auto &input : plan.inputs)
	{
		RuleWorkInput work;
		work.rate = input.rate;
		work.credit = input.credit;
		work.unit_work = input.unit_work;
		if (!input.property.empty())
		{
			const auto &state = prepare_custom_property_owner(quote.prepared, input.property_owner);
			work.available =
			    std::get<std::int64_t>(state.bases.at(input.property)) - property_catalogue->at(input.property).minimum;
		}
		else
		{
			auto payer = entities.at(input.payer);
			if (input.network)
				quote.prepared.payments.try_emplace(input.payer, payer.get<ShipResources>().network);
			auto resolved = named_stock(payer, input.store,
			                            input.network ? &quote.prepared.payments.at(input.payer).network : nullptr);
			work.available = quote.prepared.stocks.view(resolved.stock).available(input.resource);
			if (input.electrical != no_resource)
			{
				work.blocked = !quote.prepared.payments.at(input.payer).network.operation_ready(input.electrical);
				work.available =
				    std::min(work.available,
				             quote.prepared.payments.at(input.payer).network.external_payment_budget(input.electrical));
			}
		}
		inputs.push_back(work);
	}
	plan.payment = quote_rule_work(inputs, proportional);
	if (plan.payment.output < 1000)
		return plan.payment.output;
	std::vector<PreparedEffectCost> debits;
	for (std::size_t i = 0; i < plan.inputs.size(); ++i)
	{
		const auto &input = plan.inputs[i];
		PreparedEffectCost cost;
		cost.payer = input.payer;
		cost.store = input.store;
		cost.resource = input.resource;
		cost.amount = plan.payment.debits[i];
		cost.energy_store = input.electrical;
		cost.property = input.property;
		if (!input.property.empty())
			cost.property_owner = input.property_owner;
		debits.push_back(std::move(cost));
	}
	const auto frame = process_effect_invocation(quote.owner, quote.frozen, {});
	stage_effect_costs(quote.prepared, frame, debits);
	auto &credits = plan.process_work ? quote.frozen.work_credits : quote.frozen.idle_credits;
	for (std::size_t i = 0; i < plan.inputs.size(); ++i)
	{
		const auto &input = plan.inputs[i];
		auto found = std::find_if(credits.begin(), credits.end(),
		                          [&](const auto &entry)
		                          {
			                          return same_rule_work_input(entry, input);
		                          });
		if (found == credits.end())
			credits.push_back({input.payer, input.store, input.resource, plan.payment.credits[i], input.property,
			                   input.property_owner.equipment, input.property_owner.officer,
			                   input.property_owner.compartment, input.unit_work});
		else
			found->prepaid = plan.payment.credits[i];
	}
	quote.payments.push_back(std::move(plan));
	return 1000;
}
void fail_finite_native_work(const CustomPropertyAddress &owner, const std::string &id, const std::string &reason)
{
	const auto *state = process_state(owner, id);
	if (state && process_definition(*state).shortage == "stop")
	{
		const auto driver = state->parent_process.empty() ? state->id : state->parent_process;
		(void)stop_process(owner, driver, true);
		if (auto *current = process_state(owner, driver))
			current->blocker = reason;
	}
	else
		set_process_paused(owner, id, reason);
}
std::optional<FiniteNativeWork> prepare_finite_native_work(EntityId entity, std::string_view kind,
                                                           const std::string &native, const std::string &target,
                                                           const RuleActor &target_actor, std::int64_t progress,
                                                           std::int64_t required, std::int64_t desired,
                                                           std::span<const EffectCostDefinition> native_costs = {},
                                                           bool scale_rate = true)
{
	if (desired <= 0 || desired > resource_limit || progress < 0 || required < 0 || target.empty() ||
	    target.size() > 512)
		return {};
	const auto driver = finite_native_driver(entity, kind, native, &target_actor);
	if (!driver.state)
		return {};
	const auto id = begin_finite_native_run(driver.owner, driver.state->id, target, target_actor, progress, required);
	if (id.empty())
		return {};
	const auto *state = process_state(driver.owner, id);
	if (!state || !state->admitted)
		return {};
	const auto &definition = process_definition(*state);
	const auto scale = process_scale(definition.work_scale, process_expression_context(driver.owner, *state));
	if (!scale)
		return {};
	const auto rate_scale = scale_rate ? state->rate_permille : 1000;
	if (desired > std::numeric_limits<std::int64_t>::max() / std::max<std::int64_t>(1, rate_scale))
		return {};
	desired = std::min<std::int64_t>(resource_limit, desired * rate_scale / 1000);
	// Clinical/fault work has a finite target counter. Structural teams retain
	// their native aggregate actuation/fraction semantics at the repair boundary.
	if (required > 0 && (kind != "repair" || target_actor.kind == PayloadOutcomeKind::System))
		desired = std::min(desired, std::max<std::int64_t>(0, required - progress));
	while (desired > 0)
	{
		FiniteNativeWork quote;
		quote.owner = driver.owner;
		quote.frozen = *state;
		quote.work = desired;
		quote.prepared = prepare_effect_groups({});
		quote.prepared.defer_damage = true;
		try
		{
			const auto plan_for = [&](std::span<const EffectCostDefinition> costs, std::int64_t multiplier,
			                          std::int64_t denominator, const std::vector<RuleWorkCredit> &credits, bool work)
			{
				RuleUpkeepPlan plan;
				plan.context = process_context(quote.owner, quote.frozen);
				plan.costs.assign(costs.begin(), costs.end());
				for (auto &cost : plan.costs)
					cost.amount = process_cost_product(cost.amount, multiplier);
				plan.unit_work = denominator;
				plan.credits = credits;
				plan.process_owner = quote.owner;
				plan.process = quote.frozen.id;
				plan.process_work = work;
				return plan;
			};
			// Late native demand pays upkeep once for each contributing run in this frame.
			if (quote.frozen.actual_work == 0)
			{
				const auto idle_scale =
				    process_scale(definition.upkeep_scale, process_expression_context(quote.owner, quote.frozen));
				if (!idle_scale ||
				    stage_finite_native_costs(quote,
				                              plan_for(definition.upkeep_costs, *idle_scale, resource_work_unit * 1000,
				                                       quote.frozen.idle_credits, false),
				                              false) < 1000)
					throw std::runtime_error("Insufficient process upkeep");
			}
			const auto output =
			    stage_finite_native_costs(quote,
			                              plan_for(definition.work_costs, process_cost_product(desired, *scale),
			                                       definition.work_divisor * 1000000, quote.frozen.work_credits, true),
			                              definition.shortage == "proportional");
			if (output < 1000)
			{
				desired = desired * output / 1000;
				continue;
			}
			if (!native_costs.empty())
			{
				EffectGroupDefinition fees;
				fees.costs.assign(native_costs.begin(), native_costs.end());
				const auto frame = process_effect_invocation(quote.owner, quote.frozen, fees);
				quote.prepared =
				    prepare_effect_groups(std::span<const EffectGroupInvocation>(&frame, 1), std::move(quote.prepared));
				if (!quote.prepared.failure.empty())
					throw std::runtime_error(quote.prepared.failure);
			}
			return quote;
		}
		catch (const std::exception &error)
		{
			fail_finite_native_work(driver.owner, id, error.what());
			return {};
		}
	}
	fail_finite_native_work(driver.owner, id, "Insufficient process work funding");
	return {};
}
bool commit_finite_native_payment(FiniteNativeWork &quote)
{
	if (quote.paid)
		return false;
	const auto evaluation = process_work_effects(quote.owner, quote.frozen, quote.work, 1000);
	if (evaluation.status != RuleEvaluationStatus::Ready)
	{
		fail_finite_native_work(quote.owner, quote.frozen.id, "Invalid process work effect");
		return false;
	}
	std::vector<EffectGroupInvocation> frames;
	if (const auto lifecycle = process_funding_effect(quote.owner, quote.frozen, 1000, 1000))
		frames.push_back(process_effect_invocation(quote.owner, quote.frozen, *lifecycle));
	frames.push_back(process_effect_invocation(quote.owner, quote.frozen, evaluation.quote.effects));
	quote.prepared = prepare_effect_groups(frames, std::move(quote.prepared));
	const auto failure = commit_effect_group(quote.prepared);
	if (!failure.empty())
	{
		fail_finite_native_work(quote.owner, quote.frozen.id, failure);
		return false;
	}
	auto *state = process_state(quote.owner, quote.frozen.id);
	if (state && state->generation == quote.frozen.generation && state->run == quote.frozen.run && state->admitted &&
	    state->requested)
	{
		for (const auto &plan : quote.payments)
		{
			auto &credits = plan.process_work ? state->work_credits : state->idle_credits;
			for (std::size_t i = 0; i < plan.inputs.size(); ++i)
			{
				const auto &input = plan.inputs[i];
				auto credit = std::find_if(credits.begin(), credits.end(),
				                           [&](const auto &value)
				                           {
					                           return same_rule_work_input(value, input);
				                           });
				if (credit == credits.end())
					credits.push_back({input.payer, input.store, input.resource, plan.payment.credits[i],
					                   input.property, input.property_owner.equipment, input.property_owner.officer,
					                   input.property_owner.compartment, input.unit_work});
				else
					credit->prepaid = plan.payment.credits[i];
			}
		}
		(void)stage_process_funding_state(quote.owner, *state, 1000, 1000);
		state->work_remainder = process_next_work_remainder(quote.owner, quote.frozen, quote.work);
	}
	quote.paid = true;
	return true;
}
void commit_finite_native_work(const FiniteNativeWork &quote, std::int64_t actual, std::int64_t progress,
                               std::int64_t required, bool completed)
{
	if (!quote.paid || actual < 0 || actual > quote.work)
		throw std::runtime_error("Invalid committed finite native work");
	auto *state = process_state(quote.owner, quote.frozen.id);
	const bool active = state && state->generation == quote.frozen.generation && state->run == quote.frozen.run &&
	                    state->admitted && state->requested;
	if (active)
	{
		state->progress = std::clamp(progress, std::int64_t{0}, std::max(required, progress));
		state->required_work = required;
		state->ready = completed;
		state->work_output = actual ? 1000 : 0;
		state->actual_work += actual;
		state->last_work = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	}
	// Native work and payment already committed atomically. Its frozen receipt
	// survives removal of the granting process by that same work effect.
	auto *runtime = process_runtime(quote.owner);
	if (!runtime)
		return;
	const auto &definition = process_definition(quote.frozen);
	if (actual)
	{
		auto receipt = std::find_if(runtime->work.begin(), runtime->work.end(),
		                            [&](const auto &entry)
		                            {
			                            return entry.execution == quote.frozen.id &&
			                                   entry.generation == quote.frozen.generation &&
			                                   entry.activation == quote.frozen.run;
		                            });
		if (receipt == runtime->work.end())
			runtime->work.push_back({process_owner_kind(quote.owner), process_owner_name(quote.owner),
			                         definition.family, definition.id, quote.frozen.run, 1000, quote.frozen.id, actual,
			                         definition.work_unit, quote.frozen.generation});
		else
			receipt->work += actual;
	}
	runtime->work_tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	if (completed && active)
		commit_finite_process_completion(quote.owner, quote.frozen.id, quote.frozen.target);
}

NativeStructureOutcome &finite_structure_outcome(FiniteNativeWork &quote, EntityId target)
{
	const auto original = native_structure_outcome(entities.at(target).get<CombatStructure>());
	quote.prepared.original_structures.try_emplace(target, original);
	return quote.prepared.structures.try_emplace(target, original).first->second;
}
NativePatientOutcome &finite_patient_outcome(FiniteNativeWork &quote, EntityId target, std::size_t patient)
{
	const auto key = std::pair{target, patient};
	const auto original = native_patient_outcome(entities.at(target).get<PersonnelState>().officers.at(patient));
	quote.prepared.original_patients.try_emplace(key, original);
	return quote.prepared.patients.try_emplace(key, original).first->second;
}
NativeTeamOutcome &finite_team_outcome(FiniteNativeWork &quote, EntityId target, const std::string &team)
{
	const auto key = std::pair{target, team};
	const auto original = native_team_outcome(away_equipment(target, team).get<CombatMechanism>().get<TeamState>());
	quote.prepared.original_clinical_teams.try_emplace(key, original);
	return quote.prepared.clinical_teams.try_emplace(key, original).first->second;
}

void finalise_finite_native_jobs()
{
	for (const auto &[entity, ship] : entities)
		for (const auto &address : custom_property_addresses(entity))
			if (auto *runtime = process_runtime(address))
			{
				std::vector<std::string> idle;
				for (const auto &[id, state] : runtime->processes)
					if (state.admitted &&
					    (process_definition(state).native >= NativeProcessKind::Repair &&
					     process_definition(state).native <= NativeProcessKind::ServicingAmmunition) &&
					    !state.actual_work)
						idle.push_back(id);
				for (const auto &id : idle)
					set_process_paused(address, id, "No eligible native work");
			}
}

void cancel_finite_native_owner_jobs(EntityId entity, const std::string &native)
{
	for (const auto &address : custom_property_addresses(entity))
		if (auto *runtime = process_runtime(address))
		{
			std::vector<std::string> drivers;
			for (const auto &[id, state] : runtime->processes)
				if (state.parent_process.empty() && state.native_id == native &&
				    (process_definition(state).native == NativeProcessKind::Repair ||
				     process_definition(state).native == NativeProcessKind::Medical))
					drivers.push_back(id);
			for (const auto &id : drivers)
			{
				(void)stop_process(address, id, false);
				(void)process_request(address, id, true);
			}
		}
}
