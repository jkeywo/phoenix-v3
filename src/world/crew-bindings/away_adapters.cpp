module;
#include "native_headers.hpp"

module phoenix.simulation;
import :away_adapters;

namespace phoenix::world_detail
{
NativeAwayActors::Ship::Ship(Runtime &owner, EntityId id) : owner(owner), id(id)
{
}

NativeAwayActors::Ship::~Ship() = default;

AwayTransportLayout NativeAwayActors::Ship::layout() const
{
	const auto &d = owner.definition(id);
	return {d.personnel,  d.transport,
	        d.craft_bays, d.displacements,
	        d.docking,    d.combat ? &*d.combat : nullptr,
	        d.consoles,   d.craft ? &*d.craft : nullptr};
}

PersonnelState &NativeAwayActors::Ship::personnel()
{
	return owner.entities.at(id).get_mut<PersonnelState>();
}

const PersonnelState &NativeAwayActors::Ship::personnel() const
{
	return owner.entities.at(id).get<PersonnelState>();
}

CombatMechanism &NativeAwayActors::Ship::team(const std::string &key)
{
	return owner.away_equipment(id, key).get_mut<CombatMechanism>();
}

bool NativeAwayActors::Ship::team_present(const std::string &key) const
{
	return bool(owner.away_equipment(id, key));
}

const CombatMechanism &NativeAwayActors::Ship::team(const std::string &key) const
{
	return owner.away_equipment(id, key).get<CombatMechanism>();
}

const CombatStructure &NativeAwayActors::Ship::structure()
{
	return owner.entities.at(id).get<CombatStructure>();
}

std::optional<AwayTeamMotion> NativeAwayActors::Ship::team_motion(const std::string &key)
{
	const auto ship = owner.entities.at(id);
	auto &structure = ship.get_mut<CombatStructure>();
	const auto child = owner.away_equipment(id, key);
	auto &mechanism = child.get_mut<CombatMechanism>();
	if (owner.combat_dead(ship) || !child.get<Equipment>().enabled ||
	    owner.structurally_disabled(ship, mechanism.definition.id) || !owner.team_available(mechanism))
		return std::nullopt;
	const auto &state = mechanism.get<TeamState>();
	return AwayTeamMotion{structure, mechanism,
	                      state.travel <= 0 || owner.physical_edge_open(id, state.location, state.edge_to)};
}

AwayTeamRoutes NativeAwayActors::Ship::team_routes()
{
	const auto &structure = owner.entities.at(id).get<CombatStructure>();
	AwayTeamRoutes result{structure.compartments.size(), {}};
	for (const auto &edge : structure.definition.connections)
		if (owner.physical_edge_open(id, edge.from, edge.to))
			result.open_links.push_back(edge);
	return result;
}

AwayPersonnelRoutes NativeAwayActors::Ship::personnel_routes()
{
	const auto &personnel = owner.definition(id).personnel;
	AwayPersonnelRoutes result{personnel.compartments.size(), {}};
	for (const auto &edge : personnel.links)
		if (owner.physical_edge_open(id, edge.from, edge.to))
			result.open_links.push_back(edge);
	return result;
}

const DockConnection *NativeAwayActors::Ship::dock_connection() const
{
	return owner.entities.at(id).try_get<DockConnection>();
}

bool NativeAwayActors::Ship::departed()
{
	return owner.departed(id);
}

bool NativeAwayActors::Ship::dead()
{
	return owner.combat_dead(owner.entities.at(id));
}

bool NativeAwayActors::Ship::dock_ready(const AwayDefinition &definition)
{
	return owner.away_dock_ready(id, definition);
}

bool NativeAwayActors::Ship::system_operational(const std::string &system)
{
	return owner.away_equipment(id, system).get<Equipment>().enabled &&
	       !owner.structurally_disabled(owner.entities.at(id), system) &&
	       owner.fault_effectiveness(owner.entities.at(id), system) > 0;
}

std::int64_t NativeAwayActors::Ship::officer_repair_work(const std::string &mission, const std::string &officer)
{
	return owner.effective_property(owner.entities.at(id), "away:" + mission + ":officer:" + officer,
	                                PropertyKey::repair, 1000);
}

std::int64_t NativeAwayActors::Ship::structural_work(const std::vector<std::string> &teams, EntityId target,
                                                     std::int64_t compartment, const std::string &key,
                                                     std::int64_t progress, std::int64_t required)
{
	std::vector<flecs::entity> actors;
	for (const auto &team : teams)
		actors.push_back(owner.away_equipment(id, team));
	return owner.structural_repair_work(owner.entities.at(id), std::move(actors), target, compartment, key, progress,
	                                    required);
}

NativeAwayActors::Craft::Craft(Runtime &owner, EntityId id) : owner(owner), id(id)
{
}

NativeAwayActors::Craft::~Craft() = default;

CraftState &NativeAwayActors::Craft::state()
{
	return owner.entities.at(id).get_mut<CraftState>();
}

const CraftState &NativeAwayActors::Craft::state() const
{
	return owner.entities.at(id).get<CraftState>();
}

bool NativeAwayActors::Craft::dormant()
{
	return owner.craft_dormant(owner.entities.at(id));
}

bool NativeAwayActors::Craft::available(const std::string &mission)
{
	return owner.craft_available(id, mission);
}

void NativeAwayActors::Craft::launch()
{
	owner.launch_craft(owner.entities.at(id));
}

void NativeAwayActors::Craft::return_home()
{
	owner.craft_return(owner.entities.at(id));
}

NativeAwayActors::NativeAwayActors(Runtime &owner) : owner(owner)
{
}

NativeAwayActors::~NativeAwayActors() = default;

AwayShipActor &NativeAwayActors::ship(EntityId id)
{
	return ships.try_emplace(id, owner, id).first->second;
}

AwayCraftActor &NativeAwayActors::craft(EntityId id)
{
	return crafts.try_emplace(id, owner, id).first->second;
}

const AwayShipActor &NativeAwayActors::ship(EntityId id) const
{
	return ships.try_emplace(id, owner, id).first->second;
}

const AwayCraftActor &NativeAwayActors::craft(EntityId id) const
{
	return crafts.try_emplace(id, owner, id).first->second;
}

bool NativeAwayActors::is_craft(EntityId id) const
{
	return owner.entities.contains(id) && owner.entities.at(id).has<CraftState>();
}

EntityId NativeAwayActors::transport(EntityId ship, const std::string &key) const
{
	return owner.away_transport_entity(ship, key);
}

bool NativeAwayActors::contains(EntityId id)
{
	return owner.entities.contains(id);
}

EntityId NativeAwayActors::transport(EntityId ship, const std::string &key)
{
	return owner.away_transport_entity(ship, key);
}

std::int64_t NativeAwayActors::distance(EntityId left, EntityId right)
{
	return owner.distance_3d(left, right);
}

NativeAwayPeers::NativeAwayPeers(Runtime &owner) : owner(owner)
{
}

NativeAwayPeers::~NativeAwayPeers() = default;

ConversationId NativeAwayPeers::conversation(const AwayDefinition &definition)
{
	return owner.away_conversation(definition);
}

void NativeAwayPeers::prepare_contributions(std::size_t index)
{
	owner.prepare_away_tick_contributions(index);
}

bool NativeAwayPeers::assisted_send(std::size_t index)
{
	return owner.prepare_away_assisted_send(index);
}

bool NativeAwayPeers::boarding_visible(EntityId origin, EntityId target)
{
	return owner.boarding_visible(origin, target);
}

void NativeAwayPeers::advance_supply(std::size_t index)
{
	owner.advance_supply_mission(index);
}

void NativeAwayPeers::advance_craft_transport(std::size_t index, EntityId craft)
{
	owner.advance_craft_transport(index, craft);
}

void NativeAwayPeers::advance_boarding(std::size_t index)
{
	owner.advance_boarding(index);
}

void NativeAwayPeers::advance_medical(std::size_t index)
{
	owner.advance_away_medical(index);
}

bool NativeAwayPeers::boarding_action(std::size_t index, const std::string &action, const std::string &argument)
{
	return owner.boarding_action(index, action, argument);
}

bool NativeAwayPeers::has_capability(EntityId ship, const AwayMission &mission, const std::string &capability)
{
	return owner.away_has_capability(ship, mission, capability);
}

std::string NativeAwayPeers::blocker(std::size_t index)
{
	return owner.away_blocker(index);
}

bool NativeAwayPeers::automatic_repair(EntityId ship, const std::string &team)
{
	const auto &definition = owner.away_equipment(ship, team).get<CombatMechanism>().definition.get<TeamDefinition>();
	return !owner.manual_workload(owner.enabled_system_owner(ship, team),
	                              definition.capabilities.contains("medical") ? "medical" : "team_work", team);
}

void NativeAwayPeers::record_insertion(EntityId origin, EntityId target)
{
	owner.record_interdiction_insertion(origin, target);
}

void NativeAwayPeers::claim_target(std::size_t index, const std::string &claim)
{
	owner.claim_away_target(index, claim);
}

void NativeAwayPeers::execute_effect(const std::string &group, const std::string &identity,
                                     const NativeRuleContext &context)
{
	owner.execute_tree_effect(group, identity, context);
}

void NativeAwayPeers::mission_transition(const std::string &kind, EntityId source, EntityId subject,
                                         const std::string &operation, const std::string &detail)
{
	owner.mission_transition(kind, source, subject, operation, detail);
}

void NativeAwayPeers::mission_change(const std::string &key, const std::string &value, const std::string &kind,
                                     EntityId source, EntityId subject, const std::string &operation,
                                     const std::string &detail)
{
	owner.mission_change(key, value, kind, source, subject, operation, detail);
}

void NativeAwayPeers::emit(EntityId ship, const std::string &event)
{
	owner.scenario->runtime().emit({owner.conversation_instance, ++owner.conversation_event_sequence, 0}, event,
	                               {{"ship", static_cast<std::int64_t>(ship)}});
}

NativeAwayReadActors::Ship::Ship(const Runtime &owner, EntityId id) : owner(owner), id(id)
{
}

NativeAwayReadActors::Ship::~Ship() = default;

AwayTransportLayout NativeAwayReadActors::Ship::layout() const
{
	const auto &d = owner.definition(id);
	return {d.personnel,  d.transport,
	        d.craft_bays, d.displacements,
	        d.docking,    d.combat ? &*d.combat : nullptr,
	        d.consoles,   d.craft ? &*d.craft : nullptr};
}

const PersonnelState &NativeAwayReadActors::Ship::personnel() const
{
	return owner.entities.at(id).get<PersonnelState>();
}

bool NativeAwayReadActors::Ship::team_present(const std::string &key) const
{
	return bool(owner.away_equipment(id, key));
}

const CombatMechanism &NativeAwayReadActors::Ship::team(const std::string &key) const
{
	return owner.away_equipment(id, key).get<CombatMechanism>();
}

const DockConnection *NativeAwayReadActors::Ship::dock_connection() const
{
	return owner.entities.at(id).try_get<DockConnection>();
}

NativeAwayReadActors::Craft::Craft(const Runtime &owner, EntityId id) : owner(owner), id(id)
{
}

NativeAwayReadActors::Craft::~Craft() = default;

const CraftState &NativeAwayReadActors::Craft::state() const
{
	return owner.entities.at(id).get<CraftState>();
}

NativeAwayReadActors::NativeAwayReadActors(const Runtime &owner) : owner(owner)
{
}

NativeAwayReadActors::~NativeAwayReadActors() = default;

const AwayShipView &NativeAwayReadActors::ship(EntityId id) const
{
	return ships.try_emplace(id, owner, id).first->second;
}

const AwayCraftView &NativeAwayReadActors::craft(EntityId id) const
{
	return crafts.try_emplace(id, owner, id).first->second;
}

bool NativeAwayReadActors::is_craft(EntityId id) const
{
	return owner.entities.contains(id) && owner.entities.at(id).has<CraftState>();
}

EntityId NativeAwayReadActors::transport(EntityId ship, const std::string &key) const
{
	return owner.away_transport_entity(ship, key);
}

NativeAwayReadBinding::NativeAwayReadBinding(const Runtime &owner)
    : actors(owner), observation{actors, owner.slot_entities}
{
}

NativeAwayReadBinding::~NativeAwayReadBinding() = default;

NativeAwayBinding::NativeAwayBinding(Runtime &owner)
    : actors(owner), peers(owner), execution{actors,
                                             peers,
                                             owner.conversations,
                                             owner.scenario ? &owner.scenario->runtime() : nullptr,
                                             owner.slot_entities,
                                             rate,
                                             owner.personnel_postings}
{
}

NativeAwayBinding::~NativeAwayBinding() = default;

NativeAwayRestoreChecks::NativeAwayRestoreChecks(const Runtime &owner) : owner(owner)
{
}

NativeAwayRestoreChecks::~NativeAwayRestoreChecks() = default;

void NativeAwayRestoreChecks::boarding(const AwayDefinition &definition, const AwayMission &mission) const
{
	owner.validate_boarding_state(definition, mission);
}

NativeAwayPersistenceBinding::NativeAwayPersistenceBinding(const Runtime &owner)
    : read(owner), checks(owner),
      context{read.observation, {owner.slot_entities, owner.personnel_postings, owner.mission_ledger}, checks}
{
}

NativeAwayPersistenceBinding::~NativeAwayPersistenceBinding() = default;

NativeAwayProjectionSource::NativeAwayProjectionSource(const Runtime &owner) : owner(owner)
{
}

NativeAwayProjectionSource::~NativeAwayProjectionSource() = default;

AwayPermittedRow NativeAwayProjectionSource::permitted_row(std::size_t mission_index,
                                                           const ConsoleAddress &viewer) const
{
	return owner.permitted_away_row(mission_index, viewer);
}
} // namespace phoenix::world_detail
