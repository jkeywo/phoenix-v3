ConsoleAddress team_owner(EntityId ship, const std::string &team) const
{
	const auto &d = definition(ship);
	if (d.layout)
		for (const auto &controller : d.layout->controllers)
			for (const auto &command : controller.commands)
				if (command.action == OperatingAction::SystemEnabled)
					for (auto consumer : command.consumers)
						if (d.fitted->values.at(consumer).id == team)
							return {ship, controller.console};
	return system_owner(ship, team);
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
	refresh_personnel_bonuses(ps, definition(ship).personnel);
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
			for (const auto &[system, slots] : duty->effects)
				if (fault_effectiveness(ship, system) <= policy.heavy_below_permille)
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
bool patient_eligible(const OfficerState &o) const
{
	return (o.condition == "minor" || o.condition == "severe") && o.facility.empty() && o.escort.empty();
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
	const auto ps = personnel_snapshot(ship);
	for (const auto &o : ps.officers)
		if (o.location == compartment && patient_eligible(o) &&
		    order.patient_states.contains(o.condition == "severe" && o.stabilised ? "stabilised" : o.condition))
			return true;
	return false;
}
bool team_command(const ConsoleAddress &a, const OperatingCommand &c, bool execute, bool ai = false)
{
	if (!c.channel.empty() || c.request_id || !c.arguments.empty() || !c.speaker_name.empty() ||
	    !c.speaker_avatar.empty() || c.text.size() > 256 || combat_dead(entities.at(a.ship)))
		return false;
	const auto fit = instance(entities.at(a.ship), c.system);
	if (!fit || fit->kind != OperatingSystem::Team || team_owner(a.ship, c.system) != a ||
	    away_reserved(a.ship, "team:" + c.system))
		return false;
	bool authority = false;
	for (const auto &ctrl : entities.at(a.ship).get<EntityControls>().controllers)
		if (ctrl.definition.console == a.console && ctrl.definition.id == c.controller && ctrl.human != ai &&
		    ctrl.generation == c.generation)
			for (const auto &channel : ctrl.definition.commands)
				for (auto consumer : channel.consumers)
					if (definition(a.ship).fitted->values.at(consumer).id == c.system)
						authority = true;
	if (!authority)
		return false;
	auto &m = fit->entity.get_mut<CombatMechanism>();
	auto &r = m.get<TeamState>();
	if (c.value < 1 || static_cast<std::uint64_t>(c.value) != r.revision || r.revision >= 9007199254740991ULL)
		return false;
	const TeamOrderDefinition *order = nullptr;
	std::size_t target = 0;
	std::string patient;
	if (c.action == "team.order")
	{
		const auto slash = c.text.find('/');
		if (slash == std::string::npos)
			return false;
		order = team_order_definition(m, c.text.substr(0, slash));
		if (!order)
			return false;
		const auto next = c.text.find('/', slash + 1);
		const auto compartment =
		    c.text.substr(slash + 1, next == std::string::npos ? std::string::npos : next - slash - 1);
		const auto &pd = definition(a.ship).personnel;
		auto at = std::find(pd.compartments.begin(), pd.compartments.end(), compartment);
		if (at == pd.compartments.end())
			return false;
		target = at - pd.compartments.begin();
		if (!team_target_eligible(a.ship, *order, target))
			return false;
		if (next != std::string::npos)
		{
			patient = c.text.substr(next + 1);
			const auto ps = personnel_snapshot(a.ship);
			bool found = false;
			for (std::size_t i = 0; i < pd.officers.size(); ++i)
				if (pd.officers[i].id == patient && ps.officers[i].location == target &&
				    patient_eligible(ps.officers[i]))
					found = true;
			if (!found || order->target != "patients")
				return false;
		}
		const auto from = r.travel ? r.edge_to : r.location;
		if (from != static_cast<std::int64_t>(target) &&
		    repair_route(entities.at(a.ship).get<CombatStructure>(), from, target).first < 0)
			return false;
	}
	else if (c.action != "team.cancel" || !c.text.empty())
		return false;
	if (!execute)
		return true;
	// Releasing an escort preserves the patient's current position and progress.
	for (auto &o : entities.at(a.ship).get_mut<PersonnelState>().officers)
		if (o.escort == c.system)
		{
			o.escort.clear();
			o.edge_to = -1;
			o.remaining = 0;
		}
	r.order = order ? order->id : std::string{};
	r.patient = patient;
	r.activity.clear();
	r.work_done = false;
	r.destination = order ? static_cast<std::int64_t>(target) : -1;
	r.repair_system = -1;
	r.repair_auto = !order;
	r.continuation = {};
	++r.revision;
	return true;
}
std::optional<std::size_t> triage_patient(EntityId target, std::size_t compartment, const std::string &priority,
                                          const std::set<std::pair<EntityId, std::size_t>> &claimed) const
{
	const auto &pd = definition(target).personnel;
	const auto snapshot = personnel_snapshot(target);
	std::optional<std::size_t> best;
	std::tuple<int, int, std::string> score;
	for (std::size_t i = 0; i < pd.officers.size(); ++i)
	{
		const auto &o = snapshot.officers[i];
		if (o.location != compartment || !patient_eligible(o) || claimed.contains({target, i}) ||
		    !away_officer_location(target, pd.officers[i].id).empty())
			continue;
		const auto candidate = std::tuple{o.condition == "severe" && !o.stabilised ? 3
		                                  : o.condition == "minor"                 ? 2
		                                                                           : 1,
		                                  pd.officers[i].id == priority ? 1 : 0, pd.officers[i].id};
		if (!best || candidate > score)
		{
			best = i;
			score = candidate;
		}
	}
	return best;
}
void treat_patient(EntityId origin, const CombatMechanism &team, EntityId target, std::size_t patient,
                   const std::string &activity)
{
	auto &o = entities.at(target).get_mut<PersonnelState>().officers.at(patient);
	const auto &d = team.definition.get<TeamDefinition>();
	if (!d.capabilities.contains("medical") || !patient_eligible(o))
		return;
	if (activity == "stabilise" && o.condition != "severe")
		return;
	if (o.condition == "severe" && o.stabilised)
		return;
	const auto ticks = o.condition == "severe" ? d.stabilise_ticks : d.treatment_ticks;
	o.treatment += officer_value(origin, team.definition.id, "medical", 1000);
	if (o.treatment < ticks * 1000)
		return;
	o.treatment = 0;
	if (o.condition == "severe")
		o.stabilised = true;
	else
		o.condition = "healthy";
	personnel_changed(target);
}
void step_medical_facilities()
{
	for (const auto &[id, ship] : entities)
	{
		if (!ship.has<PersonnelState>() || combat_dead(ship))
			continue;
		const auto &pd = definition(id).personnel;
		auto &ps = ship.get_mut<PersonnelState>();
		for (auto &o : ps.officers)
			if (!o.facility.empty())
				for (const auto &f : pd.facilities)
					if (f.id == o.facility)
					{
						const auto at = std::find(pd.compartments.begin(), pd.compartments.end(), f.compartment) -
						                pd.compartments.begin();
						if (ship.has<CombatStructure>() && ship.get<CombatStructure>().compartments.at(at).disabled)
							continue;
						o.recovery += 1000;
						if (o.recovery >= f.recovery_ticks * 1000)
						{
							o.condition = "healthy";
							o.stabilised = false;
							o.recovery = 0;
							o.facility.clear();
							personnel_changed(id);
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
			    !child.get<Equipment>().enabled || !fault_effectiveness(ship, m->definition.id))
				continue;
			auto &r = m->get<TeamState>();
			const auto &d = m->definition.get<TeamDefinition>();
			// Medical teams default to their first authored order, just as legacy teams default to repair.
			if (r.order.empty() && r.repair_auto && !d.capabilities.contains("repair") && !d.orders.empty())
			{
				for (std::size_t c = 0; c < definition(id).personnel.compartments.size(); ++c)
					if (team_target_eligible(id, d.orders.front(), c))
					{
						r.order = d.orders.front().id;
						r.destination = c;
						r.repair_auto = false;
						r.continuation = state_tree::Instance{};
						++r.revision;
						break;
					}
			}
			const auto *order = team_order_definition(*m, r.order);
			if (!order)
				continue;
			const bool arrived = advance_repair_team(ship, child) && r.destination >= 0 && r.location == r.destination;
			if (r.destination >= 0 && r.location != r.destination && !r.travel)
				route_repair_team(ship, r, r.destination);
			using state_tree::Value;
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
			const std::vector<Value> obs{Value::boolean(arrived), Value::boolean(r.work_done), Value::boolean(eligible),
			                             Value::integer(ecs.get<Clock>().tick)};
			auto outputs = state_tree::step(order->program, r.continuation, obs);
			for (const auto &o : outputs.outputs)
				r.activity = order->program.registry.actions.at(o.action).name;
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
				auto patient = escorted ? escorted : triage_patient(id, r.location, r.patient, claimed);
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
				if (auto patient = triage_patient(id, r.location, r.patient, claimed))
				{
					claimed.insert({id, *patient});
					treat_patient(id, *m, id, *patient, r.activity);
				}
				const auto snapshot = personnel_snapshot(id);
				r.work_done = std::none_of(snapshot.officers.begin(), snapshot.officers.end(),
				                           [&](const auto &p)
				                           {
					                           return p.location == static_cast<std::size_t>(r.location) &&
					                                  patient_eligible(p) &&
					                                  ((p.condition == "severe" && !p.stabilised) ||
					                                   (p.condition == "minor" && r.activity == "treat"));
				                           });
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
		if (!m || m->definition.kind != OperatingSystem::Team || team_owner(a.ship, m->definition.id) != a)
			continue;
		const auto &r = m->get<TeamState>();
		const auto &d = m->definition.get<TeamDefinition>();
		Json row{{"id", m->definition.id}, {"type", d.type},
		         {"revision", r.revision}, {"order", r.order},
		         {"activity", r.activity}, {"reserved", away_reserved(a.ship, "team:" + m->definition.id)},
		         {"location", r.location}, {"destination", r.destination},
		         {"orders", Json::array()}};
		for (const auto &o : d.orders)
		{
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
			const auto ps = personnel_snapshot(a.ship);
			for (std::size_t i = 0; i < pd.officers.size(); ++i)
				if (ps.officers[i].condition != "healthy" && away_officer_location(a.ship, pd.officers[i].id).empty())
					row["patients"].push_back({{"id", pd.officers[i].id},
					                           {"name", pd.officers[i].name},
					                           {"condition", ps.officers[i].condition},
					                           {"location", ps.officers[i].location},
					                           {"stabilised", ps.officers[i].stabilised},
					                           {"facility", ps.officers[i].facility}});
		}
		out.push_back(std::move(row));
	}
	return out;
}

bool team_map_allowed(const ConsoleAddress &a) const
{
	for (auto child : entities.at(a.ship).get<EquipmentIndex>().values)
		if (const auto *m = child.try_get<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Team &&
		                                                      team_owner(a.ship, m->definition.id) == a &&
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
			if (!team.get<Equipment>().enabled || !td.capabilities.contains("medical"))
				continue;
			const auto order = std::find_if(td.orders.begin(), td.orders.end(),
			                                [](const auto &o)
			                                {
				                                return o.target == "patients";
			                                });
			if (order == td.orders.end())
				continue;
			auto &care = mission.care[mechanism.definition.id];
			if (care.order.empty())
				care.order = order->id;
			if (care.travel > 0)
			{
				if (--care.travel > 0)
					continue;
				care.location = care.edge_to;
				care.edge_to = -1;
			}
			const auto ps = personnel_snapshot(target);
			std::optional<std::size_t> destination;
			for (std::size_t c = 0; c < pd.compartments.size(); ++c)
				for (const auto &p : ps.officers)
					if (p.location == c && patient_eligible(p) && (p.condition == "minor" || !p.stabilised))
					{
						destination = c;
						break;
					}
			if (destination && care.location != static_cast<std::int64_t>(*destination))
			{
				const auto route = compartment_route(pd.compartments.size(), pd.links, care.location, *destination);
				if (route.first < 0)
					continue;
				care.edge_to = route.first;
				care.travel = route.second;
				continue;
			}
			if (entities.at(target).has<CombatStructure>() &&
			    entities.at(target).get<CombatStructure>().compartments.at(care.location).disabled)
				continue;
			if (destination && care.done)
			{
				care.done = false;
				care.continuation = {};
			}
			using state_tree::Value;
			const std::vector<Value> observations{Value::boolean(true), Value::boolean(!destination),
			                                      Value::boolean(destination.has_value()),
			                                      Value::integer(ecs.get<Clock>().tick)};
			const auto outputs = state_tree::step(order->program, care.continuation, observations);
			for (const auto &output : outputs.outputs)
			{
				const auto &action = order->program.registry.actions.at(output.action).name;
				if (action == "complete")
					care.done = true;
				if (action != "treat" && action != "stabilise")
					continue;
				if (auto patient = triage_patient(target, care.location, {}, claimed))
				{
					claimed.insert({target, *patient});
					const auto condition = ps.officers[*patient].condition;
					const auto stabilised = ps.officers[*patient].stabilised;
					treat_patient(origin, mechanism, target, *patient, action);
					const auto &after = entities.at(target).get<PersonnelState>().officers.at(*patient);
					if (after.condition != condition || after.stabilised != stabilised)
						if (const auto conversation = away_conversation(definition_mission))
							conversations.report(conversation, "medical");
				}
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
					if (!r.patient.empty() && std::none_of(pd.officers.begin(), pd.officers.end(),
					                                       [&](const auto &o)
					                                       {
						                                       return o.id == r.patient;
					                                       }))
						throw std::runtime_error("Unknown priority patient");
				}
		}
}

bool duty_affects_system(EntityId id, const PersonnelDuty &duty, const std::string &system) const
{
	if (duty.team == system || duty.effects.contains(system))
		return true;
	for (const auto &c : definition(id).layout->controllers)
		if (c.console == duty.console && c.id == duty.controller)
			for (const auto &command : c.commands)
				for (auto consumer : command.consumers)
					if (definition(id).fitted->values.at(consumer).id == system)
						return true;
	return false;
}
