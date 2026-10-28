ConsoleAddress enabled_system_owner(EntityId ship, const std::string &system) const
{
	const auto &d = definition(ship);
	if (d.layout)
		for (const auto &controller : d.layout->controllers)
			for (const auto &command : controller.commands)
				if (command.action == OperatingAction::SystemEnabled)
					for (auto consumer : command.consumers)
						if (d.fitted->values.at(consumer).id == system)
							return {ship, controller.console};
	return system_owner(ship, system);
}
// World adapter: authored team orders use the same physical routes, work and personnel as repair/away work.
const TeamOrderDefinition *team_order_definition(const CombatMechanism &m, const std::string &id) const
{
	for (const auto &o : m.definition.get<TeamDefinition>().orders)
		if (o.id == id)
			return &o;
	return nullptr;
}
void personnel_changed(EntityId ship)
{
	auto &ps = entities.at(ship).get_mut<PersonnelState>();
	++ps.revision;
}
void personnel_casualties(flecs::entity ship, std::size_t compartment, std::int64_t damage)
{
	if (!ship.has<PersonnelState>())
		return;
	const auto id = ship.get<StableId>().id;
	const auto &pd = definition(id).personnel;
	const auto &policy = pd.casualties;
	if (damage < policy.significant_damage)
		return;
	const auto &structure = ship.get<CombatStructure>();
	const auto before = structure.compartments.at(compartment).hp;
	const auto maximum = structure.definition.compartments.at(compartment).maximum;
	const auto after = std::max<std::int64_t>(0, before - damage);
	// Significant tier crossings or authored lethal impacts, never a roll for each damage tick.
	const bool lethal = policy.lethal && damage >= policy.lethal_damage;
	if (before * 4 / maximum == after * 4 / maximum && !lethal)
		return;
	const bool heavy = before * 1000 <= maximum * policy.heavy_below_permille;
	auto snapshot = personnel_snapshot(id);
	auto &ps = ship.get_mut<PersonnelState>();
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	for (std::size_t i = 0; i < ps.officers.size(); ++i)
	{
		auto &o = ps.officers[i];
		if (o.condition == "dead" || o.casualty_tick == tick || snapshot.officers[i].location != compartment ||
		    !away_officer_location(id, pd.officers[i].id).empty())
			continue;
		// Reserve officers are not exposed by an assigned duty.
		if (o.duty.empty() && o.facility.empty() && o.escort.empty())
			continue;
		o.location = snapshot.officers[i].location;
		o.edge_to = snapshot.officers[i].edge_to;
		o.remaining = snapshot.officers[i].remaining;
		o.casualty_tick = tick;
		auto &random = ship.get_mut<CombatStructure>().random;
		random = random * 6364136223846793005ULL + 1;
		auto roll = static_cast<std::int64_t>((random >> 16) % 1000);
		bool system_heavy = heavy;
		if (const auto *duty = personnel_duty(id, o.duty))
			for (const auto &system : definition(id).fitted->values)
				if (duty_affects_system(id, *duty, system.id) &&
				    fault_effectiveness(ship, system.id) <= policy.heavy_below_permille)
					system_heavy = true;
		const auto death = lethal && system_heavy ? policy.killed : 0;
		std::string condition;
		if (roll < death)
			condition = "dead";
		else if ((roll -= death) < policy.severe)
			condition = "severe";
		else if ((roll -= policy.severe) < policy.minor)
			condition = "minor";
		else if ((roll -= policy.minor) < policy.stun)
			condition = "stunned";
		if (condition.empty() || (o.condition == "severe" && condition != "dead") ||
		    (o.condition == "minor" && condition == "stunned"))
			continue;
		o.condition = condition;
		o.stun_until = condition == "stunned" ? tick + 20 * rate : 0;
		o.stabilised = false;
		o.treatment = o.recovery = 0;
		o.facility.clear();
		if (condition == "dead")
		{
			o.duty.clear();
			o.present = false;
			o.edge_to = -1;
			o.remaining = 0;
			o.escort.clear();
		}
		if (condition == "severe")
		{
			o.present = false;
			o.edge_to = -1;
			o.remaining = 0;
			o.escort.clear();
		}
		personnel_changed(id);
	}
}
bool team_available(const CombatMechanism &m) const
{
	const auto &r = m.get<TeamState>();
	return !r.detained && r.condition != "lost";
}
// Injury output joins other comparable modifiers. Loss/detention remain hard eligibility.
std::int64_t team_work(flecs::entity ship, const CombatMechanism &team, PropertyKey property, std::int64_t base) const
{
	const auto &state = team.get<TeamState>();
	const auto equipment = away_equipment(ship.get<StableId>().id, team.definition.id);
	if (state.detained || state.condition == "lost" || !equipment || !equipment.get<Equipment>().enabled ||
	    structurally_disabled(ship, team.definition.id))
		return 0;
	const auto injury =
	    state.condition == "injured" ? team.definition.get<TeamDefinition>().injured_effectiveness - 1000 : 0;
	return effective_property(ship, team.definition.id, property, base, injury);
}
ClinicalRoster clinical_roster(EntityId ship) const
{
	const auto ps = personnel_snapshot(ship);
	const auto &pd = definition(ship).personnel;
	ClinicalRoster roster(pd, ps);
	for (std::size_t i = 0; i < pd.officers.size(); ++i)
		roster.officer_away(i, !away_officer_location(ship, pd.officers[i].id).empty());
	for (auto child : entities.at(ship).get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Team)
		{
			const auto &r = m->get<TeamState>();
			bool away = false;
			for (std::size_t i = 0; i < away_missions.size(); ++i)
				if (away_ship(away_definitions[i]) == ship && away_missions[i].deployed())
					for (const auto &[slot, asset] : away_missions[i].assignments)
						if (asset == "team:" + m->definition.id)
							away = true;
			roster.append_team(m->definition.id, r.location, r.condition, away);
		}
	return roster;
}
std::vector<ClinicalPatient> clinical_patients(EntityId ship) const
{
	return std::move(clinical_roster(ship)).take_patients();
}
bool patient_eligible(const OfficerState &o) const
{
	return clinical_patient_eligible(o);
}
// Order filters and action restrictions apply before severity/individual priority.
bool patient_matches(EntityId target, std::size_t patient, const OfficerState &o, const TeamOrderDefinition &order,
                     const std::string &activity = {}, bool with_party = false) const
{
	const bool away = !with_party && patient < definition(target).personnel.officers.size() &&
	                  !away_officer_location(target, definition(target).personnel.officers.at(patient).id).empty();
	return clinical_patient_matches(o, order, activity, with_party, away);
}
void step_team_order(const TeamOrderDefinition &order, state_tree::Instance &continuation, std::string &activity,
                     bool arrived, bool work_done, bool eligible, EntityId origin, const std::string &team,
                     EntityId target, std::int64_t compartment)
{
	using state_tree::Value;
	const std::vector<Value> observations{Value::boolean(arrived), Value::boolean(work_done), Value::boolean(eligible),
	                                      Value::integer(ecs.get<Clock>().tick)};
	const auto outputs = state_tree::step(order.program, continuation, observations);
	for (const auto &output : outputs.outputs)
	{
		const auto &action = order.program.registry.actions.at(output.action).name;
		if (action != "apply_effect_group")
		{
			activity = action;
			continue;
		}
		NativeRuleContext context;
		context.owner = context.source = context.instigator = {origin, team};
		context.subject = context.target = {output.arguments.at(1).text == "self" ? origin : target};
		if (output.arguments.at(1).text == "target" && compartment >= 0)
		{
			context.subject.kind = context.target.kind = PayloadOutcomeKind::Compartment;
			context.subject.compartment = context.target.compartment = compartment;
		}
		context.hit = rule_hit_context(context.target);
		context.tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
		const auto scope = "team." + std::to_string(origin) + "." + team + "." + order.id + "." +
		                   std::to_string(target) + "." + std::to_string(compartment) + ".";
		// Effect tasks are once per activation; the saved continuation owns their fired state.
		execute_tree_effect(output.arguments.at(0).text, tree_effect_identity(output, scope), context);
	}
}
bool team_target_eligible(EntityId ship, const TeamOrderDefinition &order, std::size_t compartment) const
{
	const auto &pd = definition(ship).personnel;
	if (compartment >= pd.compartments.size())
		return false;
	if (order.target == "compartments")
		return true;
	if (order.target == "damage")
		return entities.at(ship).has<CombatStructure>() && repair_candidate(entities.at(ship), compartment).score > 0;
	const auto ps = clinical_patients(ship);
	for (std::size_t i = 0; i < ps.size(); ++i)
		if (ps[i].state.location == compartment && patient_matches(ship, i, ps[i].state, order))
			return true;
	return false;
}
bool team_command(const ConsoleAddress &a, const OperatingCommand &c, bool execute, bool ai = false)
{
	if (!c.channel.empty() || c.request_id || !c.arguments.empty() || !c.speaker_name.empty() ||
	    !c.speaker_avatar.empty() || c.text.size() > 256 || combat_dead(entities.at(a.ship)))
		return false;
	const auto fit = instance(entities.at(a.ship), c.system);
	if (!fit || fit->kind != OperatingSystem::Team || enabled_system_owner(a.ship, c.system) != a ||
	    away_reserved(a.ship, "team:" + c.system))
		return false;
	const bool destination_order =
	    c.action == "team.cancel" || (c.action == "team.order" && c.text.starts_with("auto/"));
	const bool assisted_destination = !ai && destination_order && manual_team_destination(a);
	bool authority = false;
	for (const auto &ctrl : entities.at(a.ship).get<EntityControls>().controllers)
		if (ctrl.definition.console == a.console && ctrl.definition.id == c.controller &&
		    (ctrl.human != ai || assisted_destination) && ctrl.generation == c.generation)
			for (const auto &channel : ctrl.definition.commands)
				for (auto consumer : channel.consumers)
					if (definition(a.ship).fitted->values.at(consumer).id == c.system)
						authority = true;
	if (!authority)
		return false;
	auto &m = fit->entity.get_mut<CombatMechanism>();
	auto &r = m.get<TeamState>();
	if (!team_available(m) || c.value < 1 || static_cast<std::uint64_t>(c.value) != r.revision ||
	    r.revision >= 9007199254740991ULL)
		return false;
	const TeamOrderDefinition *order = nullptr;
	std::size_t target = 0;
	std::string patient;
	const bool clinical = m.definition.get<TeamDefinition>().capabilities.contains("medical");
	bool assignment = false;
	if (c.action == "team.order")
	{
		const auto slash = c.text.find('/');
		if (slash == std::string::npos)
			return false;
		assignment = (clinical ||
		              (assisted_destination && m.definition.get<TeamDefinition>().capabilities.contains("repair"))) &&
		             c.text.substr(0, slash) == "auto";
		order = team_order_definition(m, c.text.substr(0, slash));
		if ((!order && !assignment) || (!ai && clinical && !assignment && !manual_workload(a, "medical")))
			return false;
		const auto next = c.text.find('/', slash + 1);
		const auto compartment =
		    c.text.substr(slash + 1, next == std::string::npos ? std::string::npos : next - slash - 1);
		const auto &pd = definition(a.ship).personnel;
		auto at = std::find(pd.compartments.begin(), pd.compartments.end(), compartment);
		if (at == pd.compartments.end())
			return false;
		target = at - pd.compartments.begin();
		if (!assignment && !team_target_eligible(a.ship, *order, target))
			return false;
		if (next != std::string::npos)
		{
			if (assignment)
				return false;
			patient = c.text.substr(next + 1);
			const auto ps = clinical_patients(a.ship);
			bool found = false;
			for (std::size_t i = 0; i < ps.size(); ++i)
				if (ps[i].id == patient && ps[i].state.location == target &&
				    patient_matches(a.ship, i, ps[i].state, *order))
					found = true;
			if (!found || order->target != "patients")
				return false;
		}
		const auto from = r.travel ? r.edge_to : r.location;
		if (from != static_cast<std::int64_t>(target) && repair_route(entities.at(a.ship), from, target).first < 0)
			return false;
	}
	else if (c.action != "team.cancel" || !c.text.empty())
		return false;
	if (!execute)
		return true;
	cancel_finite_native_owner_jobs(a.ship, c.system);
	// Releasing an escort preserves the patient's current position and progress.
	for (auto &o : entities.at(a.ship).get_mut<PersonnelState>().officers)
		if (o.escort == c.system)
		{
			o.escort.clear();
			o.edge_to = -1;
			o.remaining = 0;
		}
	r.order = order ? order->id : std::string{};
	if (clinical)
	{
		const auto &workloads = entities.at(a.ship).get<EntityControls>().layout.manual_workloads;
		if (assignment || r.medical_compartment >= 0 ||
		    (workloads.contains(a.console) && workloads.at(a.console).contains("medical")))
			r.medical_compartment = c.action == "team.order" ? static_cast<std::int64_t>(target) : -1;
	}
	r.patient = patient;
	r.activity.clear();
	r.work_done = false;
	r.destination = order || assignment ? static_cast<std::int64_t>(target) : -1;
	r.repair_system = -1;
	r.repair_auto = !order && !(assignment && !clinical);
	if (!clinical && m.definition.get<TeamDefinition>().capabilities.contains("repair") && assisted_destination)
	{
		r.compartment_assignment = true;
		r.repair_auto = false;
		if (c.action == "team.cancel")
			r.destination = r.travel ? r.edge_to : r.location;
	}
	r.continuation = {};
	++r.revision;
	return true;
}
std::optional<std::size_t> triage_patient(EntityId target, std::optional<std::size_t> compartment,
                                          const TeamOrderDefinition &order, const std::string &activity,
                                          const std::string &priority,
                                          const std::set<std::pair<EntityId, std::size_t>> &claimed,
                                          const std::set<std::size_t> *party = nullptr) const
{
	return clinical_roster(target).select(target, compartment, order, activity, priority, claimed, party);
}
void treat_patient(EntityId origin, const CombatMechanism &team, EntityId target, std::size_t patient,
                   const std::string &activity)
{
	const auto desired = team_work(entities.at(origin), team, PropertyKey::medical, 1000);
	if (desired <= 0)
		return;
	const auto patients = clinical_patients(target);
	const auto &selected = patients.at(patient);
	const auto &definition_team = team.definition.get<TeamDefinition>();
	if (!selected.team.empty())
	{
		const auto &before = away_equipment(target, selected.team).get<CombatMechanism>().get<TeamState>();
		if (activity != "treat" || before.condition != "injured")
			return;
		const auto costs = native_material_work_costs(origin, team.definition.id, "treat", before.treatment > 0);
		if (!costs)
			return;
		const auto required = definition_team.treatment_ticks * 1000;
		const auto key = std::to_string(target) + "/team/" + selected.team;
		auto quote = prepare_finite_native_work(origin, "medical", team.definition.id, key, {target, selected.team},
		                                        before.treatment, required,
		                                        std::min(desired, required - before.treatment), *costs);
		if (!quote)
			return;
		auto &prepared = prepare_team_outcome(quote->prepared, target, selected.team);
		if (prepared.condition != "injured")
			return;
		prepared.treatment += quote->work;
		const auto progress = prepared.treatment;
		const bool completed = progress >= required;
		if (completed)
			native_treat_team(prepared);
		if (commit_finite_native_payment(*quote))
			commit_finite_native_work(*quote, quote->work, std::min(required, progress), required, completed);
		return;
	}
	const auto &before = entities.at(target).get<PersonnelState>().officers.at(patient);
	if (!definition_team.capabilities.contains("medical") || !patient_eligible(before) ||
	    (activity == "stabilise" && before.condition != "severe") ||
	    (before.condition == "severe" && before.stabilised))
		return;
	const auto clinical_activity = before.condition == "severe" ? "stabilise" : "treat";
	const auto required =
	    (before.condition == "severe" ? definition_team.stabilise_ticks : definition_team.treatment_ticks) * 1000;
	const auto costs = native_material_work_costs(origin, team.definition.id, clinical_activity, before.treatment > 0);
	if (!costs)
		return;
	const auto officer = definition(target).personnel.officers.at(patient).id;
	const auto key = std::to_string(target) + "/officer/" + officer + "/" + clinical_activity;
	auto quote =
	    prepare_finite_native_work(origin, "medical", team.definition.id, key, {target, {}, officer}, before.treatment,
	                               required, std::min(desired, required - before.treatment), *costs);
	if (!quote)
		return;
	auto &prepared = prepare_patient_outcome(quote->prepared, target, patient);
	if ((clinical_activity == std::string("stabilise") && (prepared.condition != "severe" || prepared.stabilised)) ||
	    (clinical_activity == std::string("treat") && prepared.condition != "minor"))
		return;
	prepared.treatment += quote->work;
	const auto progress = prepared.treatment;
	const bool completed = progress >= required;
	if (completed)
	{
		if (prepared.condition == "severe")
			native_stabilise_patient(prepared);
		else
			native_treat_patient(prepared);
	}
	if (commit_finite_native_payment(*quote))
		commit_finite_native_work(*quote, quote->work, std::min(required, progress), required, completed);
}

bool step_clinical_work(EntityId origin, const CombatMechanism &team, EntityId target, std::size_t compartment,
                        const TeamOrderDefinition &order, const std::string &activity, const std::string &priority,
                        std::set<std::pair<EntityId, std::size_t>> &claimed)
{
	const auto patient = triage_patient(target, compartment, order, activity, priority, claimed);
	if (!patient)
		return false;
	claimed.insert({target, *patient});
	const auto before = clinical_patients(target).at(*patient).state;
	treat_patient(origin, team, target, *patient, activity);
	const auto after = clinical_patients(target).at(*patient).state;
	return before.condition != after.condition || before.stabilised != after.stabilised;
}
bool clinical_work_done(EntityId target, std::optional<std::size_t> compartment, const TeamOrderDefinition &order,
                        const std::string &activity) const
{
	return !triage_patient(target, compartment, order, activity, {}, {});
}
void step_medical_facilities()
{
	for (const auto &[id, ship] : entities)
	{
		if (!ship.has<PersonnelState>() || combat_dead(ship))
			continue;
		const auto &pd = definition(id).personnel;
		const auto count = ship.get<PersonnelState>().officers.size();
		for (std::size_t patient = 0; patient < count; ++patient)
		{
			const auto &before = ship.get<PersonnelState>().officers.at(patient);
			if (before.facility.empty())
				continue;
			for (const auto &facility : pd.facilities)
				if (facility.id == before.facility)
				{
					const auto compartment =
					    std::find(pd.compartments.begin(), pd.compartments.end(), facility.compartment) -
					    pd.compartments.begin();
					if (ship.has<CombatStructure>() &&
					    ship.get<CombatStructure>().compartments.at(compartment).disabled)
						continue;
					const auto costs = native_material_work_costs(id, facility.id, "recover", before.recovery > 0);
					if (!costs)
						continue;
					const auto required = facility.recovery_ticks * 1000;
					const auto officer = pd.officers.at(patient).id;
					const auto key = std::to_string(id) + "/officer/" + officer + "/recover";
					auto quote = prepare_finite_native_work(
					    id, "recovery", facility.id, key, {id, {}, officer}, before.recovery, required,
					    std::min<std::int64_t>(1000, required - before.recovery), *costs);
					if (!quote)
						continue;
					auto &prepared = prepare_patient_outcome(quote->prepared, id, patient);
					if (prepared.condition != "severe" || !prepared.stabilised || prepared.facility != facility.id)
						continue;
					prepared.recovery += quote->work;
					const auto progress = prepared.recovery;
					const bool completed = progress >= required;
					if (completed)
						native_recover_patient(prepared);
					if (commit_finite_native_payment(*quote))
						commit_finite_native_work(*quote, quote->work, std::min(required, progress), required,
						                          completed);
				}
		}
	}
}

void step_teams()
{
	std::set<std::pair<EntityId, std::size_t>> claimed;
	for (const auto &[id, ship] : combat_rows())
	{
		if (combat_dead(ship))
			continue;
		for (auto child : ship.get<EquipmentIndex>().values)
		{
			auto *m = child.try_get_mut<CombatMechanism>();
			if (!m || m->definition.kind != OperatingSystem::Team || away_reserved(id, "team:" + m->definition.id) ||
			    !child.get<Equipment>().enabled || structurally_disabled(ship, m->definition.id) || !team_available(*m))
				continue;
			auto &r = m->get<TeamState>();
			const auto &d = m->definition.get<TeamDefinition>();
			if (!manual_workload(enabled_system_owner(id, m->definition.id), "medical"))
				r.patient.clear();
			if (r.order.empty() && !d.capabilities.contains("repair") &&
			    station_stars(enabled_system_owner(id, m->definition.id)) < 3)
				r.repair_auto = true;
			if (d.capabilities.contains("medical") && r.medical_compartment < 0 &&
			    manual_team_destination(enabled_system_owner(id, m->definition.id)))
				r.medical_compartment = r.travel ? r.edge_to : r.location;
			bool advanced = false, arrived = false;
			if (r.order.empty() && r.medical_compartment >= 0)
			{
				r.destination = r.medical_compartment;
				arrived = advance_repair_team(ship, child) && r.location == r.destination;
				advanced = true;
				if (!arrived && !r.travel)
					route_repair_team(ship, r, r.destination);
			}
			// Use the first eligible authored order: treatment can be followed by
			// recovery without leaving a stabilised patient awaiting manual dispatch.
			if (r.order.empty() && r.repair_auto && !d.capabilities.contains("repair") &&
			    !d.capabilities.contains("security") && !d.orders.empty())
			{
				for (const auto &candidate : d.orders)
				{
					if (!r.order.empty())
						break;
					if (&candidate != &d.orders.front() &&
					    (candidate.target != "patients" ||
					     (r.medical_compartment < 0 && station_stars(enabled_system_owner(id, m->definition.id)) >= 3)))
						continue;
					for (std::size_t c = 0; c < definition(id).personnel.compartments.size(); ++c)
						if ((r.medical_compartment < 0 || c == static_cast<std::size_t>(r.medical_compartment)) &&
						    team_target_eligible(id, candidate, c))
						{
							r.order = candidate.id;
							r.destination = c;
							r.repair_auto = false;
							r.work_done = false;
							r.continuation = state_tree::Instance{};
							++r.revision;
							break;
						}
				}
			}
			const auto *order = team_order_definition(*m, r.order);
			if (!order)
				continue;
			if (!advanced)
				arrived = advance_repair_team(ship, child) && r.destination >= 0 && r.location == r.destination;
			if (r.destination >= 0 && r.location != r.destination && !r.travel)
				route_repair_team(ship, r, r.destination);
			const bool eligible = r.destination >= 0 && team_target_eligible(id, *order, r.destination);
			r.work_done = r.work_done || (arrived && !eligible && order->target == "damage");
			// Escort position follows every segment, including ticks before arrival.
			for (auto &p : ship.get_mut<PersonnelState>().officers)
				if (p.escort == m->definition.id)
				{
					p.location = r.location;
					p.edge_to = r.edge_to;
					p.remaining = r.travel;
				}
			step_team_order(*order, r.continuation, r.activity, arrived, r.work_done, eligible, id, m->definition.id,
			                id, r.destination);
			if (r.activity == "complete")
			{
				for (auto &p : ship.get_mut<PersonnelState>().officers)
					if (p.escort == m->definition.id)
					{
						p.escort.clear();
						p.remaining = 0;
						p.edge_to = -1;
					}
				r.order.clear();
				r.activity.clear();
				r.destination = -1;
				r.patient.clear();
				r.repair_auto = true;
				++r.revision;
				continue;
			}
			if (!arrived)
				continue;
			if (r.activity == "repair")
				continue; // ordinary repair execution consumes the same location/work
			if (r.activity == "diagnose")
			{
				repair_candidate(ship, r.location);
				continue;
			}
			if (!d.capabilities.contains("medical") || ship.get<CombatStructure>().compartments.at(r.location).disabled)
				continue;
			auto &ps = ship.get_mut<PersonnelState>();
			const auto &pd = definition(id).personnel;
			if (r.activity == "recover")
			{
				// Continue an existing physical escort before selecting another casualty.
				std::optional<std::size_t> escorted;
				for (std::size_t i = 0; i < ps.officers.size(); ++i)
					if (ps.officers[i].escort == m->definition.id)
						escorted = i;
				auto patient =
				    escorted ? escorted : triage_patient(id, r.location, *order, r.activity, r.patient, claimed);
				if (!patient)
					continue;
				auto &p = ps.officers[*patient];
				if (p.condition != "severe" || !p.stabilised)
					continue;
				for (const auto &f : pd.facilities)
				{
					if (std::count_if(ps.officers.begin(), ps.officers.end(),
					                  [&](const auto &o)
					                  {
						                  return o.facility == f.id;
					                  }) >= f.capacity)
						continue;
					const auto target = std::find(pd.compartments.begin(), pd.compartments.end(), f.compartment) -
					                    pd.compartments.begin();
					if (ship.get<CombatStructure>().compartments.at(target).disabled)
						continue;
					if (r.location == target)
					{
						p.facility = f.id;
						p.escort.clear();
						p.edge_to = -1;
						p.remaining = 0;
						p.present = false;
						r.work_done = true;
						personnel_changed(id);
					}
					else
					{
						p.escort = m->definition.id;
						p.present = false;
						r.destination = target;
						route_repair_team(ship, r, target);
					}
					break;
				}
			}
			else if (r.activity == "treat" || r.activity == "stabilise")
			{
				step_clinical_work(id, *m, id, r.location, *order, r.activity, r.patient, claimed);
				r.work_done = clinical_work_done(id, r.location, *order, r.activity);
			}
			for (auto &p : ps.officers)
				if (p.escort == m->definition.id)
				{
					p.location = r.location;
					p.edge_to = r.edge_to;
					p.remaining = r.travel;
				}
		}
	}
}
Json team_board(const ConsoleAddress &a) const
{
	Json out = Json::array();
	const auto ship = entities.at(a.ship);
	const auto &pd = definition(a.ship).personnel;
	if (!ship.has<CombatStructure>())
		return out;
	for (auto child : ship.get<EquipmentIndex>().values)
	{
		const auto *m = child.try_get<CombatMechanism>();
		if (!m || m->definition.kind != OperatingSystem::Team || enabled_system_owner(a.ship, m->definition.id) != a)
			continue;
		const auto &r = m->get<TeamState>();
		const auto &d = m->definition.get<TeamDefinition>();
		Json row{{"id", m->definition.id},
		         {"type", d.type},
		         {"revision", r.revision},
		         {"order", r.order},
		         {"condition", r.detained ? "detained" : r.condition},
		         {"activity", r.activity},
		         {"reserved", away_reserved(a.ship, "team:" + m->definition.id)},
		         {"location", r.location},
		         {"destination", r.destination},
		         {"orders", Json::array()}};
		bool manual = false;
		for (const auto &controller : ship.get<EntityControls>().controllers)
			if (controller.human && controller.definition.console == a.console)
				for (const auto &command : controller.definition.commands)
					if (command.action == OperatingAction::SystemEnabled)
						for (auto consumer : command.consumers)
							manual |= definition(a.ship).fitted->values.at(consumer).id == m->definition.id;
		row["repair"] = d.capabilities.contains("repair");
		row["travel"] = r.travel;
		row["work"] = row.at("reserved").get<bool>()                                        ? "Away mission"
		              : !team_available(*m)                                                 ? "Unavailable"
		              : r.travel > 0 || (r.destination >= 0 && r.destination != r.location) ? "Travelling"
		              : !r.activity.empty()                                                 ? r.activity
		                                                                                    : "Idle";
		row["work_system"] = "";
		if (d.capabilities.contains("repair") && !row.at("reserved").get<bool>() && r.travel == 0 && r.location >= 0 &&
		    repair_team_present(ship, r.location) && team_available(*m))
		{
			std::string priority;
			const auto &structure = ship.get<CombatStructure>();
			if (r.repair_system >= 0)
			{
				auto selected = structure.definition.equipment_compartments.begin();
				std::advance(selected, r.repair_system);
				priority = selected->first;
			}
			const auto candidate = repair_candidate(ship, r.location, priority);
			if (candidate.fault)
			{
				row["work"] = "Repairing system";
				row["work_system"] = candidate.fault->first;
			}
			else if (candidate.score > 0)
				row["work"] = "Repairing hull";
		}
		const bool assignment_authority =
		    manual_team_destination(a) && (d.capabilities.contains("repair") || d.capabilities.contains("medical"));
		row["assignment_authority"] = assignment_authority;
		row["manual"] = manual || assignment_authority;
		row["patient_control"] = manual_workload(a, "medical");
		row["assigned_compartment"] = r.medical_compartment;
		if (d.capabilities.contains("medical") || assignment_authority)
		{
			Json assignment{
			    {"id", "auto"},
			    {"label", d.capabilities.contains("medical") ? "Assign medical team" : "Assign repair team"},
			    {"icon", "medical"},
			    {"target", "compartment"},
			    {"targets", Json::array()}};
			for (std::size_t i = 0; i < pd.compartments.size(); ++i)
				assignment["targets"].push_back(i + 1);
			row["orders"].push_back(std::move(assignment));
		}
		for (const auto &o : d.orders)
		{
			if (d.capabilities.contains("medical") && !manual_workload(a, "medical"))
				continue;
			Json order{
			    {"id", o.id}, {"label", o.label}, {"icon", o.icon}, {"target", o.target}, {"targets", Json::array()}};
			for (std::size_t i = 0; i < pd.compartments.size(); ++i)
				if (team_target_eligible(a.ship, o, i))
					order["targets"].push_back(i + 1);
			row["orders"].push_back(std::move(order));
		}
		row["patients"] = Json::array();
		if (d.capabilities.contains("medical"))
		{
			const auto ps = clinical_patients(a.ship);
			for (const auto &p : ps)
				if (p.state.condition != "healthy" && p.state.facility != "away" &&
				    (p.team.empty() ? away_officer_location(a.ship, p.id).empty() : true))
					row["patients"].push_back({{"id", p.id},
					                           {"name", p.name},
					                           {"condition", p.state.condition},
					                           {"location", p.state.location},
					                           {"stabilised", p.state.stabilised},
					                           {"facility", p.state.facility}});
		}
		out.push_back(std::move(row));
	}
	return out;
}

bool team_map_allowed(const ConsoleAddress &a) const
{
	for (auto child : entities.at(a.ship).get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Team &&
		                                                      enabled_system_owner(a.ship, m->definition.id) == a &&
		                                                      !m->definition.get<TeamDefinition>().orders.empty())
			return true;
	return false;
}

void advance_away_medical(std::size_t index)
{
	auto &mission = away_missions[index];
	const auto &definition_mission = away_definitions[index];
	const auto origin = away_ship(definition_mission), target = definition_mission.target;
	const auto &pd = definition(target).personnel;
	if (pd.compartments.empty())
		return;
	std::set<std::pair<EntityId, std::size_t>> claimed;
	for (const auto &[slot, asset] : mission.assignments)
		if (asset.starts_with("team:"))
		{
			const auto team = away_equipment(origin, asset.substr(5));
			const auto &mechanism = team.get<CombatMechanism>();
			const auto &td = mechanism.definition.get<TeamDefinition>();
			if (!team.get<Equipment>().enabled || !team_available(mechanism) || !td.capabilities.contains("medical"))
				continue;
			auto &care = mission.care[mechanism.definition.id];
			if (care.order.empty())
			{
				care.location = mission.entry_compartment;
				const auto first = std::find_if(td.orders.begin(), td.orders.end(),
				                                [](const auto &o)
				                                {
					                                return o.target == "patients";
				                                });
				if (first == td.orders.end())
				{
					mission.care.erase(mechanism.definition.id);
					continue;
				}
				care.order = first->id;
			}
			const auto *order = team_order_definition(mechanism, care.order);
			if (!order)
				continue; // Restore validates saved identities.
			if (care.travel > 0 && !physical_edge_open(target, care.location, care.edge_to))
				continue;
			if (care.travel > 0 && --care.travel == 0)
			{
				care.location = care.edge_to;
				care.edge_to = -1;
			}
			// Away field care supports treatment/stabilisation, not facility escorts.
			auto destination = [&]() -> std::optional<std::size_t>
			{
				const auto activity = care.activity == "stabilise" ? "stabilise" : "treat";
				std::optional<std::size_t> found;
				for (std::size_t c = 0; c < pd.compartments.size(); ++c)
					if (!clinical_work_done(target, c, *order, activity))
						found = c;
				return found;
			};
			auto at = destination();
			if (at && care.done)
			{
				care.done = care.work_done = false;
				care.activity.clear();
				care.continuation = {};
			}
			const bool arrived = !care.travel && (!at || care.location == static_cast<std::int64_t>(*at));
			step_team_order(*order, care.continuation, care.activity, arrived, care.work_done, at.has_value(), origin,
			                mechanism.definition.id, target, care.location);
			if (care.activity == "complete")
			{
				care.done = true;
				continue;
			}
			at = destination();
			if (at && !care.travel && care.location != static_cast<std::int64_t>(*at))
			{
				const auto route = physical_route(target, pd.compartments.size(), pd.links, care.location, *at);
				if (route.first >= 0)
				{
					care.edge_to = route.first;
					care.travel = route.second;
				}
			}
			if (care.travel || (at && care.location != static_cast<std::int64_t>(*at)))
				continue;
			if (entities.at(target).has<CombatStructure>() &&
			    entities.at(target).get<CombatStructure>().compartments.at(care.location).disabled)
				continue;
			if (care.activity == "treat" || care.activity == "stabilise")
			{
				if (step_clinical_work(origin, mechanism, target, care.location, *order, care.activity, {}, claimed))
					if (const auto conversation = away_conversation(definition_mission))
						conversations.report(conversation, "medical");
				care.work_done = clinical_work_done(target, std::nullopt, *order, care.activity);
			}
		}
}

void validate_medical_state() const
{
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	for (const auto &[id, ship] : entities)
		if (ship.has<PersonnelState>())
		{
			const auto &pd = definition(id).personnel;
			const auto &ps = ship.get<PersonnelState>();
			for (const auto &o : ps.officers)
			{
				if (o.casualty_tick > tick ||
				    (o.condition == "stunned" && (o.stun_until < tick || o.stun_until > tick + 20 * rate)) ||
				    (o.condition == "healthy" && (o.treatment || o.recovery || o.stabilised)) ||
				    (o.facility.empty() && o.recovery))
					throw std::runtime_error("Invalid clinical timing");
				if (!o.escort.empty())
				{
					const auto fit = instance(ship, o.escort);
					if (!fit || fit->kind != OperatingSystem::Team || o.condition != "severe" || !o.stabilised ||
					    o.present || !o.facility.empty())
						throw std::runtime_error("Invalid patient escort");
					const auto &r = fit->entity.get<CombatMechanism>().get<TeamState>();
					if (r.order.empty() || r.activity != "recover" ||
					    r.location != static_cast<std::int64_t>(o.location) || r.travel != o.remaining ||
					    r.edge_to != o.edge_to || away_reserved(id, "team:" + o.escort))
						throw std::runtime_error("Inconsistent patient escort");
				}
			}
			for (auto child : ship.get<EquipmentIndex>().values)
				if (const auto *m = child.try_get<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Team)
				{
					const auto &r = m->get<TeamState>();
					if (r.medical_compartment < -1 ||
					    r.medical_compartment >= static_cast<std::int64_t>(pd.compartments.size()))
						throw std::runtime_error("Invalid assigned medical compartment");
					const auto patients = clinical_patients(id);
					if (!r.patient.empty() && std::none_of(patients.begin(), patients.end(),
					                                       [&](const auto &o)
					                                       {
						                                       return o.id == r.patient;
					                                       }))
						throw std::runtime_error("Unknown priority patient");
					if ((r.condition != "healthy" && r.condition != "injured" && r.condition != "lost") ||
					    r.treatment < 0 || r.treatment > 1728000000LL || (r.condition != "injured" && r.treatment))
						throw std::runtime_error("Invalid team clinical state");
				}
		}
}

// This relation depends only on the resolved fit, not officer assignment,
// condition or Controller ownership. A new World owns a fresh definition cache.
mutable std::map<std::pair<std::string, std::string>, std::set<std::string>> duty_systems_;
bool duty_affects_system(EntityId id, const PersonnelDuty &duty, const std::string &system) const
{
	if (duty.team == system || duty.choir == system || duty.effects.contains(system))
		return true;
	const auto key = std::pair{entities.at(id).get<Definition>().name, duty.id};
	auto found = duty_systems_.find(key);
	if (found == duty_systems_.end())
	{
		std::set<std::string> systems;
		const auto &resolved = definition(id);
		for (const auto &c : resolved.layout->controllers)
			if (c.console == duty.console && c.id == duty.controller)
				for (const auto &command : c.commands)
					for (const auto consumer : command.consumers)
						systems.insert(resolved.fitted->values.at(consumer).id);
		found = duty_systems_.emplace(key, std::move(systems)).first;
	}
	return found->second.contains(system);
}
