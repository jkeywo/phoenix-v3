module;
#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>

export module phoenix.gameplay.away.runtime;
export import phoenix.gameplay.away.operations;
export import phoenix.content.controllers.types;
export import phoenix.gameplay.personnel.state;
export import phoenix.gameplay.combat.state;
export import phoenix.gameplay.craft;
export import phoenix.gameplay.docking;
export import phoenix.gameplay.repairs;
export import phoenix.conversations;
export import phoenix.scenario;
export import phoenix.effects.contracts;
export import phoenix.scenarios.objectives;

export namespace phoenix
{
struct AwayTransportLayout
{
	const PersonnelDefinition &personnel;
	const TransportDefinition &ship;
	const std::map<std::string, CraftBayDefinition> &bays;
	const std::map<std::string, DisplacementDefinition> &displacements;
	const std::vector<DockPortDefinition> &ports;
	const CombatDefinition *structure;
	const std::vector<ConsoleSpec> &consoles;
	const CraftDefinition *craft;
};
struct AwayTeamMotion
{
	CombatStructure &structure;
	CombatMechanism &mechanism;
	bool edge_open;
};
struct AwayPersonnelRoutes
{
	std::size_t compartments;
	std::vector<PersonnelDefinition::Link> open_links;
};
struct AwayTeamRoutes
{
	std::size_t compartments;
	std::vector<CompartmentConnection> open_links;
};
// Actor-bound physical access. No ECS handles, entity container or arbitrary
// component/property API crosses this consumer-owned port. Calls are synchronous.
// Observation capabilities cannot acquire mutable components or invoke peers.
class AwayShipView
{
public:
	virtual ~AwayShipView();
	virtual AwayTransportLayout layout() const = 0;
	virtual const PersonnelState &personnel() const = 0;
	virtual const CombatMechanism &team(const std::string &team_id) const = 0;
	virtual bool team_present(const std::string &team_id) const = 0;
	virtual const DockConnection *dock_connection() const = 0;
};
class AwayCraftView
{
public:
	virtual ~AwayCraftView();
	virtual const CraftState &state() const = 0;
};
class AwayViews
{
public:
	virtual ~AwayViews();
	virtual const AwayShipView &ship(EntityId ship_id) const = 0;
	virtual const AwayCraftView &craft(EntityId craft_id) const = 0;
	virtual bool is_craft(EntityId entity_id) const = 0;
	virtual EntityId transport(EntityId origin_ship, const std::string &transport_key) const = 0;
};
struct AwayObservation
{
	const AwayViews &physical;
	const std::map<std::string, EntityId> &slots;
};
class AwayShipActor : public AwayShipView
{
public:
	virtual ~AwayShipActor();
	virtual AwayTransportLayout layout() const override = 0;
	virtual PersonnelState &personnel() = 0;
	virtual const PersonnelState &personnel() const = 0;
	virtual CombatMechanism &team(const std::string &team_id) = 0;
	virtual const CombatMechanism &team(const std::string &team_id) const = 0;
	virtual const CombatStructure &structure() = 0;
	virtual std::optional<AwayTeamMotion> team_motion(const std::string &team_id) = 0;
	virtual AwayTeamRoutes team_routes() = 0;
	virtual AwayPersonnelRoutes personnel_routes() = 0;
	virtual const DockConnection *dock_connection() const override = 0;
	virtual bool departed() = 0;
	virtual bool dead() = 0;
	virtual bool dock_ready(const AwayDefinition &mission_definition) = 0;
	virtual bool system_operational(const std::string &system_id) = 0;
	virtual std::int64_t officer_repair_work(const std::string &mission_id, const std::string &officer_id) = 0;
	virtual std::int64_t structural_work(const std::vector<std::string> &team_ids, EntityId target,
	                                     std::int64_t compartment, const std::string &work_key, std::int64_t progress,
	                                     std::int64_t required) = 0;
};
class AwayCraftActor : public AwayCraftView
{
public:
	virtual ~AwayCraftActor();
	virtual CraftState &state() = 0;
	virtual const CraftState &state() const = 0;
	virtual bool dormant() = 0;
	virtual bool available(const std::string &mission) = 0;
	virtual void launch() = 0;
	virtual void return_home() = 0;
};
class AwayActors : public AwayViews
{
public:
	virtual ~AwayActors();
	// Locator wrappers live for this execution call. Borrowed component references
	// are used immediately and may not survive a structural peer operation. Finding
	// a wrapper performs no component reads; each actor method resolves its state.
	virtual AwayShipActor &ship(EntityId ship_id) = 0;
	virtual const AwayShipActor &ship(EntityId ship_id) const override = 0;
	virtual AwayCraftActor &craft(EntityId craft_id) = 0;
	virtual const AwayCraftActor &craft(EntityId craft_id) const override = 0;
	virtual bool contains(EntityId entity_id) = 0;
	virtual EntityId transport(EntityId origin_ship, const std::string &transport_key) = 0;
	virtual EntityId transport(EntityId origin_ship, const std::string &transport_key) const override = 0;
	virtual std::int64_t distance(EntityId origin, EntityId target) = 0;
};

// Existing domain operations called at their original points in the lifecycle.
// Command admission, boarding/supply/clinical work and receipts keep their own
// canonical state. Implementations may not retain mission references.
class AwayPeers
{
public:
	virtual ~AwayPeers();
	virtual ConversationId conversation(const AwayDefinition &mission_definition) = 0;
	virtual void prepare_contributions(std::size_t mission_index) = 0;
	virtual bool assisted_send(std::size_t mission_index) = 0;
	virtual bool boarding_visible(EntityId origin, EntityId target) = 0;
	virtual void advance_supply(std::size_t mission_index) = 0;
	virtual void advance_craft_transport(std::size_t mission_index, EntityId craft_id) = 0;
	virtual void advance_boarding(std::size_t mission_index) = 0;
	virtual void advance_medical(std::size_t mission_index) = 0;
	virtual bool boarding_action(std::size_t mission_index, const std::string &action, const std::string &argument) = 0;
	virtual bool has_capability(EntityId origin, const AwayMission &mission, const std::string &capability) = 0;
	virtual std::string blocker(std::size_t mission_index) = 0;
	virtual bool automatic_repair(EntityId origin, const std::string &team_id) = 0;
	virtual void record_insertion(EntityId origin, EntityId target) = 0;
	virtual void claim_target(std::size_t mission_index, const std::string &claim) = 0;
	virtual void execute_effect(const std::string &effect_group, const std::string &effect_identity,
	                            const NativeRuleContext &context) = 0;
	virtual void mission_transition(const std::string &kind, EntityId source, EntityId subject,
	                                const std::string &operation, const std::string &detail) = 0;
	virtual void mission_change(const std::string &state_key, const std::string &state_value, const std::string &kind,
	                            EntityId source, EntityId subject, const std::string &operation,
	                            const std::string &detail) = 0;
	virtual void emit(EntityId origin, const std::string &event_name) = 0;
};
struct AwayExecution
{
	AwayActors &physical;
	AwayPeers &peers;
	ConversationRuntime &conversations;
	ScenarioRuntime *scenario;
	const std::map<std::string, EntityId> &slots;
	std::int64_t ticks_per_second;
	std::map<EntityId, std::map<std::string, PersonnelPosting>> &postings;
};
struct AwayReservations
{
	const std::map<std::string, EntityId> &slots;
	const std::map<EntityId, std::map<std::string, PersonnelPosting>> &postings;
	const MissionLedger &ledger;
};
class AwayRestoreChecks
{
public:
	virtual ~AwayRestoreChecks();
	// Boarding owns capture/defender receipts; called after care validation at the
	// original restore boundary, before the mission continuation is installed.
	virtual void boarding(const AwayDefinition &definition, const AwayMission &mission) const = 0;
};
struct AwayRestoreContext
{
	AwayObservation observation;
	AwayReservations reservations;
	const AwayRestoreChecks &checks;
};
struct AwayPermittedSlot
{
	bool owned = false;
	std::vector<std::string> choices;
};
struct AwayPermittedSupply
{
	std::int64_t quantity = 0, payment_quantity = 0;
	std::string phase;
	std::int64_t delivered = 0;
	bool manual = false;
	std::vector<std::int64_t> quantities;
};
// Only ownership/choice decisions cross from the control adapter. Canonical
// mission identity, disclosure, phase, result and assignment fields stay here.
struct AwayPermittedRow
{
	bool dispatch = false, automatic_dispatch = false, can_contribute = false, transport_owned = false;
	std::string blocker, owner, dispatch_system;
	std::vector<AwayPermittedSlot> slots, craft_slots;
	std::vector<std::string> transports;
	std::optional<AwayPermittedSupply> supply;
	std::map<std::string, std::string> labels;
};
class AwayProjectionSource
{
public:
	virtual ~AwayProjectionSource();
	// Runtime checks origin and Hidden disclosure before calling this readonly
	// source. Collection preserves existing native control admission predicates.
	virtual AwayPermittedRow permitted_row(std::size_t mission_index, const ConsoleAddress &viewer) const = 0;
};

class AwayRuntime : public AwayOperations
{
public:
	AwayRuntime();
	~AwayRuntime();
	AwayRuntime(const AwayRuntime &other);
	AwayRuntime &operator=(const AwayRuntime &other);
	AwayRuntime(AwayRuntime &&other) noexcept;
	AwayRuntime &operator=(AwayRuntime &&other) noexcept;
	void stage(EntityId origin, AwayMission &mission, AwayExecution &execution);
	void advance(std::uint64_t tick, AwayExecution &execution);
	bool staged(EntityId origin, const AwayMission &mission, const AwayObservation &observation) const;
	std::size_t staging_location(EntityId origin, const std::string &transport_key,
	                             const AwayObservation &observation) const;
	std::int64_t transfer_ticks(const AwayDefinition &definition, const AwayMission &mission,
	                            const AwayObservation &observation) const;
	void recover(EntityId origin, AwayMission &mission, AwayExecution &execution);
	void deploy(const AwayDefinition &definition, AwayMission &mission, const AwayObservation &observation);
	Json save(const AwayObservation &observation) const;
	void restore(const Json &rows, const AwayRestoreContext &context);
	bool reserved(EntityId origin, const std::string &asset, const AwayReservations &context) const;
	bool slot_accepts(EntityId origin, const AwaySlot &slot, const std::string &asset,
	                  const AwayObservation &observation) const;
	Json board(const ConsoleAddress &viewer, const AwayObservation &observation,
	           const AwayProjectionSource &permitted) const;
};
} // namespace phoenix
