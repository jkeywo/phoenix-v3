module;
#include "native_headers.hpp"

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::boarding_visible(EntityId origin, EntityId target) const -> bool
{
	if (!entities.contains(target) || combat_dead(entities.at(target)) || departed(target) || origin == target)
		return false;
	const auto &a = entities.at(origin).get<FactionMembership>().id;
	const auto &b = entities.at(target).get<FactionMembership>().id;
	if (operational_owner(origin) == operational_owner(target) || (!a.empty() && a == b))
		return false;
	for (const auto &seat : definition(origin).consoles)
		for (const auto &contact : capture_console_contacts({origin, seat.id}))
			if (contact.id == target)
				return true;
	return false;
}

auto Runtime::refresh_boarding_templates() -> void
{
	for (const auto &[origin, ship] : entities)
	{
		if (origin >= field_identity_base || !ship.has<EquipmentIndex>())
			continue;
		bool dispatch = false;
		for (auto child : ship.get<EquipmentIndex>().values)
			dispatch |= child.get<Equipment>().kind == OperatingSystem::Dispatch;
		if (!dispatch)
			continue;
		for (const auto &[target, other] : entities)
			if (target != origin && target < field_identity_base)
				for (const auto &b : definition(target).boarding)
				{
					const auto id = "board." + std::to_string(origin) + "." + std::to_string(target) + "." + b.id;
					if (std::any_of(away_operations.definitions().begin(), away_operations.definitions().end(),
					                [&](const auto &d)
					                {
						                return d.id == id;
					                }))
						continue;
					if (away_operations.definitions().size() >= 256)
						throw std::runtime_error("Too many instantiated away operations");
					AwayDefinition d;
					d.id = d.conversation = id;
					d.title = b.title;
					d.origin = origin;
					d.target = target;
					d.retain_craft = b.retain_craft;
					d.boarding = b;
					d.docking_ports = definition(target).boarding_docking_ports.at(b.id);
					d.allow_ship_transport = !d.docking_ports.empty();
					d.program = b.program;
					d.slots = b.slots;
					d.repair_compartments = b.compartments;
					away_operations.append_definition(d);
					const auto character = "party-" + id;
					const auto participant = "ship:" + std::to_string(target) + "/" + character;
					conversations.participants.emplace(
					    participant, CommsParticipant{participant, "Boarding party", {}, {}, target, true, false});
					ConversationStart c;
					c.id = id;
					c.tree = b.dialogue;
					c.title = b.title;
					c.start_event = id;
					c.bindings["ship"] = {origin, {}, {}};
					c.bindings["party"] = {target, {}, character};
					c.members = {"ship", "party"};
					conversation_starts.push_back(c);
				}
	}
}

auto Runtime::boarding_conversation_requested(const std::string &id) const -> bool
{
	return away_operations.conversation_requested(id);
}

auto Runtime::boarding_defender_reserved(EntityId target, const std::string &asset) const -> bool
{
	return away_operations.defender_reserved(target, asset);
}

auto Runtime::boarding_party_teams(EntityId origin, const AwayMission &m, const std::string &capability) const
    -> std::vector<flecs::entity>
{
	std::vector<flecs::entity> out;
	for (const auto &[slot, asset] : m.assignments)
		if (asset.starts_with("team:"))
		{
			const auto team = away_equipment(origin, asset.substr(5));
			const auto &mechanism = team.get<CombatMechanism>();
			if (team.get<Equipment>().enabled && team_available(mechanism) &&
			    (capability.empty() || mechanism.definition.get<TeamDefinition>().capabilities.contains(capability)))
				out.push_back(team);
		}
	return out;
}

auto Runtime::boarding_roll(EntityId target) -> std::int64_t
{
	auto &random = entities.at(target).get_mut<CombatStructure>().random;
	return boarding_random_roll(random);
}

auto Runtime::boarding_casualty(EntityId ship, flecs::entity team, const BoardingEncounter &e, bool medical) -> void
{
	auto &mechanism = team.get_mut<CombatMechanism>();
	auto &state = mechanism.get<TeamState>();
	auto &random = entities.at(ship).get_mut<CombatStructure>().random;
	const auto &definition_personnel = definition(ship).personnel;
	auto &personnel = entities.at(ship).get_mut<PersonnelState>();
	apply_boarding_casualties(
	    {ship, mechanism.definition.id, state, definition_personnel, personnel, random, mission_ledger.lost}, e,
	    medical);
}

auto Runtime::boarding_field_care(EntityId origin, AwayMission &mission, bool can_work) -> void
{
	std::set<std::size_t> party;
	const auto patients = clinical_patients(origin);
	for (std::size_t i = 0; i < patients.size(); ++i)
	{
		const auto &p = patients[i];
		bool present =
		    std::find(mission.accompanying.begin(), mission.accompanying.end(), p.id) != mission.accompanying.end();
		for (const auto &[slot, asset] : mission.assignments)
			present |= asset == (p.team.empty() ? "officer:" + p.id : "team:" + p.team);
		if (present)
			party.insert(i);
	}
	std::set<std::pair<EntityId, std::size_t>> claimed;
	for (auto healer : boarding_party_teams(origin, mission, "medical"))
	{
		const auto &mechanism = healer.get<CombatMechanism>();
		const auto &orders = mechanism.definition.get<TeamDefinition>().orders;
		auto &care = mission.care[mechanism.definition.id];
		if (care.order.empty())
		{
			const auto first = std::find_if(orders.begin(), orders.end(),
			                                [](const auto &o)
			                                {
				                                return o.target == "patients";
			                                });
			if (first == orders.end())
			{
				mission.care.erase(mechanism.definition.id);
				continue;
			}
			care.order = first->id;
		}
		const auto &order = *team_order_definition(mechanism, care.order);
		auto activity = care.activity == "stabilise" ? "stabilise" : "treat";
		auto selected = triage_patient(origin, {}, order, activity, {}, claimed, &party);
		if (care.done && selected)
		{
			care.done = care.work_done = false;
			care.activity.clear();
			care.continuation = {};
		}
		step_team_order(order, care.continuation, care.activity, can_work, care.work_done, selected.has_value(), origin,
		                mechanism.definition.id, origin, selected ? patients[*selected].state.location : -1);
		if (care.activity == "complete")
		{
			care.done = true;
			continue;
		}
		if (care.activity != "treat" && care.activity != "stabilise")
			continue;
		selected = triage_patient(origin, {}, order, care.activity, {}, claimed, &party);
		if (can_work && selected)
		{
			claimed.insert({origin, *selected});
			treat_patient(origin, mechanism, origin, *selected, care.activity);
		}
		care.work_done = !triage_patient(origin, {}, order, care.activity, {}, {}, &party).has_value();
	}
}

auto Runtime::boarding_defender_available(EntityId ship, flecs::entity team) const -> bool
{
	const auto *m = team.try_get<CombatMechanism>();
	if (!m || m->definition.kind != OperatingSystem::Team || !team.get<Equipment>().enabled ||
	    !m->definition.get<TeamDefinition>().capabilities.contains("security") || !team_available(*m) ||
	    m->get<TeamState>().detained)
		return false;
	return !away_operations.asset_assigned(ship, "team:" + m->definition.id, slot_entities);
}

auto Runtime::advance_boarding(std::size_t index) -> void
{
	const auto &d = away_operations.definitions()[index];
	auto &m = away_operations.missions()[index];
	auto &s = m.boarding;
	const auto origin = away_ship(d), target = d.target;
	const auto &pd = definition(target).personnel;
	// Required Security deliberately leaves this manifest when it becomes prize crew.
	// Continue care for recoverable visitors without reopening the completed assault.
	if (s.captured)
	{
		boarding_field_care(origin, m, true);
		return;
	}
	if (boarding_party_teams(origin, m, "security").empty())
	{
		m.outcome = "fail:Boarding party lost";
		return;
	}
	const bool traversable = !s.travel || physical_edge_open(target, s.location, s.edge_to);
	std::vector<PersonnelDefinition::Link> open_links;
	if (boarding_route_required_after_tick(s, traversable))
		for (const auto &link : pd.links)
			if (physical_edge_open(target, link.from, link.to))
				open_links.push_back(link);
	if (!advance_boarding_route(s, pd.compartments.size(), open_links, traversable))
	{
		m.outcome = "fail:Boarding route unavailable";
		return;
	}
	boarding_field_care(origin, m, !s.travel && (s.encounter.empty() || s.encounter_done));
	if (s.travel)
		return;
	if (s.encounter.empty() || s.encounter_done)
	{
		return;
	}
	const auto &e = d.boarding->encounters.at(s.encounter);
	if (s.location != static_cast<std::int64_t>(d.boarding->compartments.at(e.compartment)))
		return;
	std::vector<flecs::entity> defenders;
	for (const auto &key : e.defenders)
	{
		const auto team = away_equipment(target, key);
		if (!boarding_defender_available(target, team))
			continue;
		auto &r = team.get_mut<CombatMechanism>().get<TeamState>();
		if (!advance_repair_team(entities.at(target), team))
			return;
		if (r.location != s.location)
		{
			route_repair_team(entities.at(target), r, s.location);
			return;
		}
		defenders.push_back(team);
	}
	{
		BoardingAssessmentFacts facts;
		const auto attackers = boarding_party_teams(origin, m, "security");
		for (auto attacker : attackers)
			facts.security_strength +=
			    team_work(entities.at(origin), attacker.get<CombatMechanism>(), PropertyKey::security, 1000);
		for (auto defender : defenders)
			facts.defender_penalties.push_back(team_work(entities.at(target), defender.get<CombatMechanism>(),
			                                             PropertyKey::security, e.defender_penalty));
		for (const auto &[system, penalty] : e.defences)
			if (away_equipment(target, system).get<Equipment>().enabled &&
			    !structurally_disabled(entities.at(target), system))
				facts.defence_effects.emplace_back(system,
				                                   penalty * fault_effectiveness(entities.at(target), system) / 1000);
		if (assess_boarding(s, e, facts))
			if (const auto cid = away_conversation(d); cid && !e.report.empty() && conversations.report(cid, e.report))
				s.assessment_message = conversations.conversations.at(cid).messages.back().id;
	}
	const auto cid = away_conversation(d);
	if (cid && conversations.conversations.contains(cid))
	{
		const auto &bindings = conversations.conversations.at(cid).bindings;
		for (const auto &message : conversations.conversations.at(cid).messages)
			if (message.id == s.assessment_message && message.delivered.contains(bindings.at("ship")))
				s.reported = true;
	}
	if (s.assessment > 0)
	{
		--s.assessment;
		return;
	}
	s.encounter_success = boarding_roll(target) < s.chance;
	s.encounter_done = true;
	s.outcomes[s.encounter] = s.encounter_success;
	const bool medical = !boarding_party_teams(origin, m, "medical").empty();
	for (auto attacker : boarding_party_teams(origin, m, ""))
		boarding_casualty(origin, attacker, e, medical);
	bool defending_medical = false;
	for (auto child : entities.at(target).get<EquipmentIndex>().values)
		if (const auto *mechanism = child.try_get<CombatMechanism>();
		    mechanism && mechanism->definition.kind == OperatingSystem::Team &&
		    mechanism->definition.get<TeamDefinition>().capabilities.contains("medical") &&
		    team_available(*mechanism) && child.get<Equipment>().enabled &&
		    mechanism->get<TeamState>().location == s.location && !mechanism->get<TeamState>().travel &&
		    !away_reserved(target, "team:" + mechanism->definition.id))
			defending_medical = true;
	for (auto defender : defenders)
	{
		boarding_casualty(target, defender, e, defending_medical);
		if (s.encounter_success)
			s.neutralised[defender.get<Equipment>().id] = defender.get<CombatMechanism>().get<TeamState>().revision;
	}
	if (boarding_party_teams(origin, m, "security").empty())
		m.outcome = "fail:Boarding party lost";
	if (cid)
		conversations.event(cid, s.encounter_success ? "encounter_success" : "encounter_failure");
}

auto Runtime::boarding_action(std::size_t index, const std::string &action, const std::string &key) -> bool
{
	auto &m = away_operations.missions()[index];
	const auto &d = away_operations.definitions()[index];
	auto &s = m.boarding;
	if (action != "move_to" && action != "encounter" && action != "secure" && action != "release" &&
	    action != "capture")
		return false;
	if (!m.deployed())
		throw std::runtime_error("Boarding effect requires physical deployment");
	if (action == "move_to")
	{
		s.destination = d.boarding->compartments.at(key);
		return true;
	}
	if (action == "encounter")
	{
		s.encounter = key;
		s.encounter_done = s.outcomes.contains(key);
		s.encounter_success = s.encounter_done ? s.outcomes.at(key) : false;
		s.assessed = s.reported = false;
		s.assessment_message = 0;
		s.destination = d.boarding->compartments.at(d.boarding->encounters.at(key).compartment);
		return true;
	}
	const auto &o = d.boarding->objectives.at(key);
	if (s.travel || s.location != static_cast<std::int64_t>(d.boarding->compartments.at(o.compartment)) ||
	    boarding_party_teams(away_ship(d), m, "security").empty())
	{
		m.outcome = "fail:Boarding objective unavailable";
		return true;
	}
	if (action == "release")
	{
		s.secured.erase(key);
		return true;
	}
	if (action == "secure")
	{
		if (boarding_contested(index, s.location))
		{
			m.outcome = "fail:Boarding objective contested";
			return true;
		}
		s.secured.insert(key);
		if (o.kind == "sabotage")
		{
			auto &structure = entities.at(d.target).get_mut<CombatStructure>();
			const auto &defs = structure.definition.faults.at(o.system);
			for (std::size_t n = 0; n < defs.size(); ++n)
				if (defs[n].id == o.fault)
					structure.faults.at(o.system)[n].active = true;
		}
		return true;
	}
	if (s.secured.contains(key))
	{
		if (o.prize_crew && !o.prize_capability.empty())
		{
			claim_away_target(index, o.prize_capability);
			m.boarding.captured = true;
		}
		else
			capture_boarded_ship(index, o.prize_crew);
	}
	return true;
}

auto Runtime::validate_boarding_state(const AwayDefinition &d, const AwayMission &m) const -> void
{
	if (!d.boarding)
	{
		if (Json(m.boarding) != Json(BoardingState{}))
			throw std::runtime_error("Boarding state on ordinary mission");
		return;
	}
	const auto &s = m.boarding;
	const auto n = static_cast<std::int64_t>(d.boarding->compartments.size());
	if ((s.prize_crew && !s.captured) || (s.captured && (!m.ever_deployed || !captured_by.contains(d.target))))
		throw std::runtime_error("Invalid capture continuation");
	if (s.captured && std::none_of(s.secured.begin(), s.secured.end(),
	                               [&](const auto &key)
	                               {
		                               return d.boarding->objectives.contains(key) &&
		                                      d.boarding->objectives.at(key).kind == "bridge";
	                               }))
		throw std::runtime_error("Capture without secured bridge");
	if (s.encounter_done &&
	    (s.encounter.empty() || !s.outcomes.contains(s.encounter) || s.outcomes.at(s.encounter) != s.encounter_success))
		throw std::runtime_error("Invalid resolved encounter");
	if (s.location < 0 || s.location >= n || s.destination < -1 || s.destination >= n || s.travel < 0 ||
	    s.travel > 1000000 || (s.travel > 0) != (s.edge_to >= 0) || s.edge_to >= n || s.chance < 0 || s.chance > 1000 ||
	    s.defenders < 0 || s.defenders > 128 || s.assessment < 0 || s.assessment > 1728000 || s.outcomes.size() > 32 ||
	    s.secured.size() > 32)
		throw std::runtime_error("Invalid boarding continuation");
	if (!s.encounter.empty() && !d.boarding->encounters.contains(s.encounter))
		throw std::runtime_error("Unknown boarding encounter");
	if (s.factors.size() > 35 || s.neutralised.size() > 128)
		throw std::runtime_error("Invalid boarding assessment");
	// Matches the permitted projection: 128 defenders at the native 1e9 security output cap.
	for (const auto &[key, value] : s.factors)
		if (key.size() > 64 || value < -128000000000LL || value > 128000000000LL)
			throw std::runtime_error("Invalid boarding factor");
	for (const auto &[key, revision] : s.neutralised)
	{
		const auto team = away_equipment(d.target, key);
		if (!team || team.get<Equipment>().kind != OperatingSystem::Team || !revision ||
		    revision > team.get<CombatMechanism>().get<TeamState>().revision)
			throw std::runtime_error("Invalid defender receipt");
	}
	for (const auto &[key, result] : s.outcomes)
		if (!d.boarding->encounters.contains(key))
			throw std::runtime_error("Unknown resolved encounter");
	for (const auto &key : s.secured)
		if (!d.boarding->objectives.contains(key))
			throw std::runtime_error("Unknown secured objective");
	if (s.travel)
	{
		bool valid = false;
		for (const auto &link : definition(d.target).personnel.links)
			valid |= ((link.from == s.location && link.to == s.edge_to) ||
			          (link.to == s.location && link.from == s.edge_to)) &&
			         s.travel <= link.ticks;
		if (!valid)
			throw std::runtime_error("Invalid boarding route");
	}
}

auto Runtime::capture_boarded_ship(std::size_t index, bool prize) -> void
{
	const auto &d = away_operations.definitions()[index];
	auto &m = away_operations.missions()[index];
	if (m.boarding.captured)
		return;
	const auto target = entities.at(d.target);
	const auto origin = away_ship(d);
	if (operational_owner(origin) == operational_owner(d.target))
	{
		m.outcome = "complete:Ship already secured";
		return;
	}
	set_operational_owner(d.target, origin);
	m.boarding.captured = true;
	m.boarding.prize_crew = prize;
	free_captives(d.target, operational_owner(origin));
}

auto Runtime::boarding_inhibited(EntityId target, const std::string &system) const -> bool
{
	for (std::size_t i = 0; i < away_operations.definitions().size(); ++i)
	{
		const auto &d = away_operations.definitions()[i];
		const auto &m = away_operations.missions()[i];
		if (!d.boarding || d.target != target || !m.deployed() || m.boarding.travel ||
		    boarding_party_teams(away_ship(d), m, "security").empty())
			continue;
		for (const auto &key : m.boarding.secured)
		{
			const auto &o = d.boarding->objectives.at(key);
			if (o.kind == "hold" && o.system == system &&
			    m.boarding.location == static_cast<std::int64_t>(d.boarding->compartments.at(o.compartment)) &&
			    !boarding_contested(i, m.boarding.location))
				return true;
		}
	}
	return false;
}

auto Runtime::boarding_contested(std::size_t index, std::size_t location) const -> bool
{
	const auto &d = away_operations.definitions()[index];
	const auto &s = away_operations.missions()[index].boarding;
	for (auto child : entities.at(d.target).get<EquipmentIndex>().values)
		if (boarding_defender_available(d.target, child))
		{
			const auto &m = child.get<CombatMechanism>();
			const auto &r = m.get<TeamState>();
			if (r.location == static_cast<std::int64_t>(location) && !r.travel &&
			    (!s.neutralised.contains(m.definition.id) || s.neutralised.at(m.definition.id) != r.revision))
				return true;
		}
	return false;
}

auto Runtime::operational_owner(EntityId ship) const -> EntityId
{
	std::set<EntityId> seen;
	while (captured_by.contains(ship))
	{
		if (!seen.insert(ship).second)
			throw std::runtime_error("Cyclic capture authority");
		ship = captured_by.at(ship);
	}
	return ship;
}
} // namespace phoenix::world_detail
