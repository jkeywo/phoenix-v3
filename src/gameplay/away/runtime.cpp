module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
module phoenix.gameplay.away.runtime;
import phoenix.state_tree;
import phoenix.gameplay.teams.routes;
namespace phoenix
{
AwayShipView::~AwayShipView() = default;
AwayCraftView::~AwayCraftView() = default;
AwayViews::~AwayViews() = default;
AwayShipActor::~AwayShipActor() = default;
AwayCraftActor::~AwayCraftActor() = default;
AwayActors::~AwayActors() = default;
AwayPeers::~AwayPeers() = default;
AwayRuntime::AwayRuntime() = default;
AwayRuntime::~AwayRuntime() = default;
AwayRuntime::AwayRuntime(const AwayRuntime &) = default;
AwayRuntime &AwayRuntime::operator=(const AwayRuntime &) = default;
AwayRuntime::AwayRuntime(AwayRuntime &&) noexcept = default;
AwayRuntime &AwayRuntime::operator=(AwayRuntime &&) noexcept = default;
namespace
{
bool compartment_repaired(const CombatStructure &structure, const std::map<std::string, std::size_t> &compartments,
                          const std::string &name)
{
	return structure.compartments.at(compartments.at(name)).hp ==
	       structure.definition.compartments.at(compartments.at(name)).maximum;
}
const PersonnelDuty *team_duty(const PersonnelDefinition &definition, const std::string &team)
{
	for (const auto &duty : definition.duties)
		if (duty.team == team)
			return &duty;
	return nullptr;
}
void return_escorts(AwayMission &mission, AwayActors &physical)
{
	for (const auto &[slot, id] : mission.escorts)
		if (physical.contains(id) && std::as_const(physical.craft(id)).state().phase != CraftPhase::Lost)
			physical.craft(id).state().pending_order = "return";
}
} // namespace

void AwayRuntime::stage(EntityId ship, AwayMission &m, AwayExecution &execution)
{
	const AwayObservation observation{execution.physical, execution.slots};
	auto &physical = execution.physical;
	if (m.transport.empty() || m.absent())
		return;
	const auto target = staging_location(ship, m.transport, observation);
	const auto &pd = physical.ship(ship).layout().personnel;
	for (const auto &[slot, asset] : m.assignments)
	{
		if (asset.starts_with("team:"))
		{
			const auto team = asset.substr(5);
			auto &r = physical.ship(ship).team(team).get<TeamState>();
			r.order.clear();
			r.activity.clear();
			r.patient.clear();
			r.continuation = {};
			r.repair_auto = false;
			r.repair_system = -1;
			r.destination = -1;
			const auto movement = physical.ship(ship).team_motion(team);
			if (movement && advance_native_repair_team(movement->structure, movement->mechanism, movement->edge_open) &&
			    r.location != static_cast<std::int64_t>(target))
			{
				const auto routes = physical.ship(ship).team_routes();
				const auto route = compartment_route(routes.compartments, routes.open_links, r.location, target);
				r.edge_to = route.first;
				r.travel = r.edge_total = route.second;
			}
		}
		else
		{
			auto &ps = physical.ship(ship).personnel();
			for (std::size_t i = 0; i < pd.officers.size(); ++i)
				if (pd.officers[i].id == asset.substr(8))
				{
					if (!officer_healthy(ps.officers[i]))
						continue;
					auto &o = ps.officers[i];
					if (o.remaining == 0 && o.location != target)
					{
						const auto routes = physical.ship(ship).personnel_routes();
						auto route = compartment_route(routes.compartments, routes.open_links, o.location, target);
						o.edge_to = route.first;
						o.remaining = route.second;
					}
				}
		}
	}
}
void AwayRuntime::advance(std::uint64_t tick, AwayExecution &execution)
{
	const AwayObservation observation{execution.physical, execution.slots};
	auto &physical = execution.physical;
	auto &peers = execution.peers;
	auto &conversations = execution.conversations;
	auto *scenario = execution.scenario;
	const auto rate = execution.ticks_per_second;
	auto active = active_transfers(execution.slots);
	for (std::size_t i = 0; i < missions().size(); ++i)
	{
		auto &m = missions()[i];
		const auto &d = definitions()[i];
		const auto ship = origin(d, execution.slots);
		if (!ship || (physical.ship(ship).departed() && !m.absent()) || m.phase == AwayPhase::Lost ||
		    m.phase == AwayPhase::Captured)
			continue;
		const auto cid = peers.conversation(d);
		for (auto at = m.escorts.begin(); at != m.escorts.end();)
			if (std::as_const(physical.craft(at->second)).state().phase == CraftPhase::Lost)
			{
				auto &lost = physical.craft(at->second).state();
				if (lost.mission == d.id)
				{
					lost.mission.clear();
					lost.pending_order.clear();
					lost.pending_target = 0;
					++lost.revision;
				}
				m.flags.insert("escort_lost." + at->first);
				at = m.escorts.erase(at);
				++m.revision;
			}
			else
				++at;
		if (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered)
			peers.prepare_contributions(i);
		if (m.phase == AwayPhase::Hidden)
		{
			if (d.boarding && peers.boarding_visible(ship, d.target))
			{
				m.phase = AwayPhase::Available;
				++m.revision;
			}
			// Ordinary missions retain their delivered-introduction disclosure.
			if (cid && conversations.conversations.contains(cid))
			{
				const auto &c = conversations.conversations.at(cid);
				const auto self = "ship:" + std::to_string(ship);
				for (const auto &msg : c.messages)
					if (msg.text == d.reveal_event && msg.delivered.contains(self))
					{
						m.phase = AwayPhase::Available;
						++m.revision;
						break;
					}
			}
			if (m.phase == AwayPhase::Hidden)
				continue;
		}
		if (d.supply)
		{
			if (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered)
				stage(ship, m, execution);
			peers.advance_supply(i);
			continue;
		}
		if (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered || m.phase == AwayPhase::Outbound)
			stage(ship, m, execution);
		// Activating an assisted docking mission authorises its first insertion. The normal
		// send path still checks staging, capacity, capability and the physical dock.
		if (m.phase == AwayPhase::Open && m.transport == "ship" && !d.supply && m.outcome.empty())
		{
			if (peers.assisted_send(i))
				active.try_emplace(std::pair{ship, m.transport}, i);
		}
		const auto transport = physical.transport(ship, m.transport);
		if (transport && transport != ship && m.phase == AwayPhase::Outbound &&
		    std::as_const(physical.craft(transport)).state().phase == CraftPhase::Docked &&
		    std::as_const(physical.craft(transport)).state().order == "transport" &&
		    std::all_of(m.escorts.begin(), m.escorts.end(),
		                [&](const auto &s)
		                {
			                return std::as_const(physical.craft(s.second)).state().phase == CraftPhase::Flying;
		                }))
		{
			physical.craft(transport).launch();
		}
		if (transport && transport != ship && !m.transport_released &&
		    (m.phase == AwayPhase::Outbound || m.phase == AwayPhase::Inbound || m.phase == AwayPhase::Deployed) &&
		    std::as_const(physical.craft(transport)).state().phase == CraftPhase::Lost)
		{
			m.outcome = "fail:Transport lost";
			m.phase = AwayPhase::Lost;
			return_escorts(m, physical);
			++m.revision;
		}
		if (transport && transport != ship && m.phase == AwayPhase::Outbound &&
		    std::as_const(physical.craft(transport)).state().phase == CraftPhase::Flying)
			peers.record_insertion(ship, d.target);
		if (transport && transport != ship)
			peers.advance_craft_transport(i, transport);
		else if ((m.phase == AwayPhase::Outbound || m.phase == AwayPhase::Inbound) &&
		         active.at({ship, m.transport}) == i && peers.blocker(i).empty() &&
		         (m.deployed() || staged(ship, m, observation)))
		{
			if (m.phase == AwayPhase::Outbound)
				peers.record_insertion(ship, d.target);
			if (++m.transfer >= transfer_ticks(d, m, observation))
			{
				if (m.phase == AwayPhase::Inbound)
					recover(ship, m, execution);
				else
					deploy(d, m, observation);
			}
		}
		if (m.phase == AwayPhase::Deployed)
		{
			if (d.boarding)
				peers.advance_boarding(i);
			else
				peers.advance_medical(i);
		}
		if (m.outcome.empty())
		{
			const bool work_ready = m.work_capability.empty() || peers.has_capability(ship, m, m.work_capability);
			if (m.deployed() && work_ready && m.work > 0 && m.timed_work)
				m.progress = std::min(m.work, m.progress + 1000);
			if (m.deployed() && work_ready && m.work > 0 && !m.timed_work)
			{
				std::vector<std::string> teams;
				std::int64_t officer_work = 0;
				for (const auto &[slot, asset] : m.assignments)
					if (asset.starts_with("team:"))
					{
						const auto team = asset.substr(5);
						if (std::as_const(physical.ship(ship))
						        .team(team)
						        .definition.get<TeamDefinition>()
						        .capabilities.contains("repair"))
							teams.push_back(team);
					}
					else
						for (const auto &officer : physical.ship(ship).layout().personnel.officers)
							if (asset == "officer:" + officer.id &&
							    officer_healthy(
							        std::as_const(physical.ship(ship))
							            .personnel()
							            .officers.at(&officer -
							                         physical.ship(ship).layout().personnel.officers.data())))
							{
								officer_work += physical.ship(ship).officer_repair_work(d.id, officer.id);
							}
				const auto repair_at = m.repair_compartment.empty()
				                           ? std::int64_t{-1}
				                           : static_cast<std::int64_t>(d.repair_compartments.at(m.repair_compartment));
				const auto effort = physical.ship(ship).structural_work(
				    teams, d.target, repair_at, d.id + "/" + m.work_key, m.progress * rate, m.work * rate);
				// Standalone officers can perform generic work, but structural repair needs a team.
				const auto progress = teams.empty() ? officer_work : effort / rate;
				m.progress = std::min(m.work, m.progress + progress);
			}
			const bool repaired =
			    m.repair_compartment.empty() ||
			    compartment_repaired(physical.ship(d.target).structure(), d.repair_compartments, m.repair_compartment);
			if (scenario && !(*scenario).definition().event_bindings.empty() && m.work > 0 && m.progress >= m.work &&
			    repaired && work_ready)
				peers.mission_change("work/" + std::to_string(ship) + "/" + d.id + "/" + m.work_key, "completed",
				                     "away.work_completed", ship, d.target, d.id, m.work_key);
			const auto answer = cid ? conversations.conversations.at(cid).prompt.answer : std::string{};
			using state_tree::Value;
			std::vector<Value> obs{Value::boolean(m.deployed()),
			                       Value::boolean(m.recovered),
			                       Value::boolean(m.work > 0 && m.progress >= m.work && repaired && work_ready),
			                       Value::integer(tick),
			                       Value::integer(m.progress),
			                       Value::string(answer),
			                       Value::string(m.checkpoint)};
			const bool present = !m.transport_released && transport && !physical.ship(transport).dead() &&
			                     (transport == ship ? physical.ship(ship).dock_ready(d)
			                                        : physical.distance(transport, d.target) <= d.deployment_range) &&
			                     (transport == ship || !physical.craft(transport).dormant());
			obs.push_back(Value::boolean(present));
			obs.push_back(Value::boolean(transport && !physical.ship(transport).dead() &&
			                             (transport == ship || physical.craft(transport).available(d.id))));
			for (std::size_t field = obs.size(); field < d.program.registry.observations.size() - (d.boarding ? 4 : 0);
			     ++field)
			{
				const auto &name = d.program.registry.observations[field].name;
				if (name.starts_with("flags."))
				{
					obs.push_back(Value::boolean(m.flags.contains(name.substr(6))));
					continue;
				}
				if (name.starts_with("events."))
				{
					obs.push_back(Value::boolean(scenario && (*scenario).has_event(name.substr(7), {})));
					continue;
				}
				if (name.starts_with("team_capabilities."))
				{
					obs.push_back(Value::boolean(peers.has_capability(ship, m, name.substr(18))));
					continue;
				}
				const bool operational = name.starts_with("transport.operational.");
				const auto tag = name.substr(operational ? 22 : 21);
				bool has = false;
				if (transport)
				{
					const auto &t = physical.ship(transport).layout().ship;
					has = std::find(t.capabilities.begin(), t.capabilities.end(), tag) != t.capabilities.end();
					if (operational)
					{
						has &= present;
						if (t.capability_systems.contains(tag))
							for (const auto &system : t.capability_systems.at(tag))
								has &= physical.ship(transport).system_operational(system);
					}
				}
				obs.push_back(Value::boolean(has));
			}
			if (d.boarding)
			{
				obs.push_back(Value::boolean(!m.boarding.travel && m.boarding.destination == m.boarding.location));
				obs.push_back(Value::boolean(m.boarding.encounter_done));
				obs.push_back(Value::boolean(m.boarding.encounter_success));
				obs.push_back(Value::integer(m.boarding.defenders));
			}
			const auto outputs = state_tree::step(d.program, m.continuation, obs);
			for (const auto &o : outputs.outputs)
			{
				if (!m.outcome.empty())
					break;
				if (!admit_effect(m.effects, o))
					continue;
				const auto &name = d.program.registry.actions.at(o.action).name;
				const auto &arg = o.arguments.at(0);
				if (name == "apply_effect_group")
				{
					NativeRuleContext context;
					context.owner = context.source = context.instigator = {ship};
					context.subject = context.target = {o.arguments.at(1).text == "self" ? ship : d.target};
					context.hit = PayloadHitContext{context.target.entity};
					context.tick = tick;
					peers.execute_effect(
					    arg.text, tree_effect_identity(o, "away." + d.id + "." + std::to_string(ship) + "."), context);
					continue;
				}
				if (d.boarding && peers.boarding_action(i, name, arg.text))
					continue;
				if (name == "work" || name == "timed_work")
				{
					m.timed_work = name == "timed_work";
					if (arg.number < 1 || arg.number > 1728000)
						throw std::runtime_error("Invalid away work duration");
					if (m.work_key != m.checkpoint || m.work != arg.number * 1000)
					{
						m.work_key = m.checkpoint;
						m.work = arg.number * 1000;
						m.progress = std::min(m.work, m.saved_progress[m.checkpoint]);
					}
				}
				else if (name == "restore_compartment")
				{
					if (!m.deployed())
						throw std::runtime_error("Away repairs require a deployed party");
					m.repair_compartment = arg.text;
				}
				else if (name == "checkpoint")
				{
					m.work_capability.clear();
					if (m.checkpoint != arg.text)
					{
						if (m.work > 0 && !m.work_key.empty())
							m.saved_progress[m.work_key] = m.progress;
						m.work = 0;
						m.progress = 0;
						m.repair_compartment.clear();
					}
					m.checkpoint = arg.text;
				}
				else if (name == "pause")
				{
					if (m.work > 0 && !m.work_key.empty())
						m.saved_progress[m.work_key] = m.progress;
					m.work = 0;
					m.progress = 0;
				}
				else if (name == "mark")
					m.flags.insert(arg.text);
				else if (name == "claim")
					peers.claim_target(i, arg.text);
				else if (name == "require_team")
					m.work_capability = arg.text;
				else if (name == "crew_deaths")
				{
					if (arg.number < 0 || arg.number > 4096)
						throw std::runtime_error("Invalid away casualty count");
					m.crew_deaths = arg.number;
				}
				else if (name == "offer" && cid)
					conversations.offer(cid, arg.text);
				else if (name == "say" && cid)
					conversations.report(cid, arg.text);
				else if (name == "log")
					peers.mission_transition("mission.log", ship, d.target, d.id, arg.text);
				else if ((name == "injure_duty" || name == "kill_duty") && m.deployed())
				{
					if (m.assignments.contains(arg.text) && m.assignments.at(arg.text).starts_with("team:"))
						if (const auto *duty =
						        team_duty(physical.ship(ship).layout().personnel, m.assignments.at(arg.text).substr(5)))
						{
							auto &ps = physical.ship(ship).personnel();
							const auto &pd = physical.ship(ship).layout().personnel;
							for (std::size_t oi = 0; oi < pd.officers.size(); ++oi)
								if (ps.officers[oi].duty == duty->id && ps.officers[oi].condition != "dead" &&
								    std::find(m.accompanying.begin(), m.accompanying.end(), pd.officers[oi].id) !=
								        m.accompanying.end())
								{
									auto &officer = ps.officers[oi];
									officer.condition = name == "kill_duty" ? "dead" : "severe";
									officer.present = false;
									officer.casualty_tick = tick;
									officer.stun_until = officer.treatment = officer.recovery = officer.remaining = 0;
									officer.stabilised = false;
									officer.edge_to = -1;
									officer.facility.clear();
									officer.escort.clear();
									if (name == "kill_duty")
									{
										officer.duty.clear();
										std::erase(m.accompanying, pd.officers[oi].id);
									}
									++ps.revision;
									peers.mission_transition("mission.casualty", ship, ship, pd.officers[oi].id,
									                         officer.condition);
									break;
								}
						}
				}
				else if (name == "release_transport" && m.phase == AwayPhase::Deployed && transport &&
				         transport != ship && !m.aboard_craft)
				{
					m.transport_released = true;
					physical.craft(transport).state().mission.clear();
					physical.craft(transport).return_home();
					++m.revision;
				}
				else if (name == "report" && cid)
					conversations.event(cid, arg.text);
				else if (name == "complete" || name == "fail")
				{
					m.outcome = name + ":" + arg.text;
					if (cid)
						conversations.conversations.at(cid).prompt = {};
				}
				else if (name == "emit" && scenario)
					peers.emit(ship, arg.text);
			}
		}
	}
}
bool AwayRuntime::staged(EntityId ship, const AwayMission &m, const AwayObservation &observation) const
{
	auto &physical = observation.physical;
	if (m.transport.empty())
		return false;
	const auto location = staging_location(ship, m.transport, observation);
	for (const auto &[slot, asset] : m.assignments)
		if (asset.starts_with("team:"))
		{
			const auto &r = std::as_const(physical.ship(ship)).team(asset.substr(5)).get<TeamState>();
			if (r.location != static_cast<std::int64_t>(location) || r.travel)
				return false;
		}
		else
		{
			const auto &pd = physical.ship(ship).layout().personnel;
			const auto &ps = std::as_const(physical.ship(ship)).personnel();
			for (std::size_t i = 0; i < pd.officers.size(); ++i)
				if (pd.officers[i].id == asset.substr(8) &&
				    (!officer_healthy(ps.officers[i]) || ps.officers[i].location != location ||
				     ps.officers[i].remaining))
					return false;
		}
	for (const auto &key : m.accompanying)
	{
		const auto &pd = physical.ship(ship).layout().personnel;
		const auto &ps = std::as_const(physical.ship(ship)).personnel();
		for (std::size_t i = 0; i < pd.officers.size(); ++i)
			if (pd.officers[i].id == key &&
			    (!officer_healthy(ps.officers[i]) || !ps.officers[i].present || ps.officers[i].remaining))
				return false;
	}
	return true;
}
std::size_t AwayRuntime::staging_location(EntityId ship, const std::string &transport,
                                          const AwayObservation &observation) const
{
	const auto d = observation.physical.ship(ship).layout();
	const auto physical = observation.physical.transport(ship, transport);
	if (physical == ship)
	{
		if (const auto *dock = observation.physical.ship(ship).dock_connection())
			return d.structure->compartment_ids.at(d.ports.at(dock->port).compartment);
		if (!d.ports.empty())
			return d.structure->compartment_ids.at(d.ports.front().compartment);
	}
	const auto &name = physical == ship ? d.ship.compartment
	                   : physical
	                       ? d.bays.at(std::as_const(observation.physical.craft(physical)).state().bay).compartment
	                       : d.displacements.at(transport).compartment;
	return std::find(d.personnel.compartments.begin(), d.personnel.compartments.end(), name) -
	       d.personnel.compartments.begin();
}
std::int64_t AwayRuntime::transfer_ticks(const AwayDefinition &d, const AwayMission &m,
                                         const AwayObservation &observation) const
{
	auto &physical = observation.physical;
	const auto ship = origin(d, observation.slots);
	const auto craft = physical.transport(ship, m.transport);
	if (!craft)
		return physical.ship(ship).layout().displacements.at(m.transport).ticks;
	if (craft != ship)
	{
		const auto &bay = physical.ship(ship).layout().bays.at(std::as_const(physical.craft(craft)).state().bay);
		if (bay.legacy_transport)
			return bay.transfer_ticks;
	}
	return d.deployment_ticks;
}
void AwayRuntime::recover(EntityId ship, AwayMission &m, AwayExecution &execution)
{
	const AwayObservation observation{execution.physical, execution.slots};
	auto &physical = execution.physical;
	for (const auto &[slot, asset] : m.assignments)
		execution.postings[ship].erase(asset);
	for (const auto &officer : m.accompanying)
		execution.postings[ship].erase("officer:" + officer);
	const auto location = staging_location(ship, m.transport, observation);
	for (const auto &[slot, asset] : m.assignments)
		if (asset.starts_with("team:"))
		{
			auto &r = physical.ship(ship).team(asset.substr(5)).get<TeamState>();
			r.location = location;
			r.destination = r.edge_to = -1;
			r.travel = r.edge_total = 0;
			r.order.clear();
			r.activity.clear();
			r.patient.clear();
			r.continuation = {};
			r.repair_auto = execution.peers.automatic_repair(ship, asset.substr(5));
			r.repair_system = -1;
		}
	auto &p = physical.ship(ship).personnel();
	const auto &d = physical.ship(ship).layout().personnel;
	for (std::size_t o = 0; o < d.officers.size(); ++o)
		if (std::find(m.accompanying.begin(), m.accompanying.end(), d.officers[o].id) != m.accompanying.end() ||
		    std::any_of(m.assignments.begin(), m.assignments.end(),
		                [&](const auto &a)
		                {
			                return a.second == "officer:" + d.officers[o].id;
		                }))
		{
			p.officers[o].location = location;
			p.officers[o].remaining = 0;
			p.officers[o].edge_to = -1;
		}
	++p.revision;
	AwayOperations::finish_recovery(m);
}
void AwayRuntime::deploy(const AwayDefinition &definition, AwayMission &m, const AwayObservation &observation)
{
	auto &physical = observation.physical;
	m.care.clear();
	m.entry_compartment = 0;
	if (m.transport == "ship")
	{
		const auto ship = origin(definition, observation.slots);
		if (const auto *dock = physical.ship(ship).dock_connection())
			m.entry_compartment =
			    physical.ship(definition.target)
			        .layout()
			        .structure->compartment_ids.at(
			            physical.ship(definition.target).layout().ports.at(dock->remote_port).compartment);
	}
	AwayOperations::mark_deployed(
	    m, definition.boarding
	           ? std::optional<std::int64_t>(definition.boarding->compartments.at(definition.boarding->entry))
	           : std::nullopt);
}
} // namespace phoenix
