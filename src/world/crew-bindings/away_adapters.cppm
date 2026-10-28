module;
#include "native_headers.hpp"

export module phoenix.simulation:away_adapters;
export import :runtime;

export namespace phoenix::world_detail
{
// These actor caches and execution frames belong only to Away integration.
// Keep their method bodies outside the shared Runtime interface.
struct NativeAwayActors final : AwayActors
{
	struct Ship final : AwayShipActor
	{
		Runtime &owner;
		EntityId id;
		Ship(Runtime &owner, EntityId id);
		~Ship() override;
		AwayTransportLayout layout() const override;
		PersonnelState &personnel() override;
		const PersonnelState &personnel() const override;
		CombatMechanism &team(const std::string &key) override;
		bool team_present(const std::string &key) const override;
		const CombatMechanism &team(const std::string &key) const override;
		const CombatStructure &structure() override;
		std::optional<AwayTeamMotion> team_motion(const std::string &key) override;
		AwayTeamRoutes team_routes() override;
		AwayPersonnelRoutes personnel_routes() override;
		const DockConnection *dock_connection() const override;
		bool departed() override;
		bool dead() override;
		bool dock_ready(const AwayDefinition &definition) override;
		bool system_operational(const std::string &system) override;
		std::int64_t officer_repair_work(const std::string &mission, const std::string &officer) override;
		std::int64_t structural_work(const std::vector<std::string> &teams, EntityId target, std::int64_t compartment,
		                             const std::string &key, std::int64_t progress, std::int64_t required) override;
	};
	struct Craft final : AwayCraftActor
	{
		Runtime &owner;
		EntityId id;
		Craft(Runtime &owner, EntityId id);
		~Craft() override;
		CraftState &state() override;
		const CraftState &state() const override;
		bool dormant() override;
		bool available(const std::string &mission) override;
		void launch() override;
		void return_home() override;
	};
	Runtime &owner;
	mutable std::map<EntityId, Ship> ships;
	mutable std::map<EntityId, Craft> crafts;
	explicit NativeAwayActors(Runtime &owner);
	~NativeAwayActors() override;
	AwayShipActor &ship(EntityId id) override;
	AwayCraftActor &craft(EntityId id) override;
	const AwayShipActor &ship(EntityId id) const override;
	const AwayCraftActor &craft(EntityId id) const override;
	bool is_craft(EntityId id) const override;
	EntityId transport(EntityId ship, const std::string &key) const override;
	bool contains(EntityId id) override;
	EntityId transport(EntityId ship, const std::string &key) override;
	std::int64_t distance(EntityId left, EntityId right) override;
};
struct NativeAwayPeers final : AwayPeers
{
	Runtime &owner;
	explicit NativeAwayPeers(Runtime &owner);
	~NativeAwayPeers() override;
	ConversationId conversation(const AwayDefinition &definition) override;
	void prepare_contributions(std::size_t index) override;
	bool assisted_send(std::size_t index) override;
	bool boarding_visible(EntityId origin, EntityId target) override;
	void advance_supply(std::size_t index) override;
	void advance_craft_transport(std::size_t index, EntityId craft) override;
	void advance_boarding(std::size_t index) override;
	void advance_medical(std::size_t index) override;
	bool boarding_action(std::size_t index, const std::string &action, const std::string &argument) override;
	bool has_capability(EntityId ship, const AwayMission &mission, const std::string &capability) override;
	std::string blocker(std::size_t index) override;
	bool automatic_repair(EntityId ship, const std::string &team) override;
	void record_insertion(EntityId origin, EntityId target) override;
	void claim_target(std::size_t index, const std::string &claim) override;
	void execute_effect(const std::string &group, const std::string &identity,
	                    const NativeRuleContext &context) override;
	void mission_transition(const std::string &kind, EntityId source, EntityId subject, const std::string &operation,
	                        const std::string &detail) override;
	void mission_change(const std::string &key, const std::string &value, const std::string &kind, EntityId source,
	                    EntityId subject, const std::string &operation, const std::string &detail) override;
	void emit(EntityId ship, const std::string &event) override;
};

// Query actors are intentionally separate from execution actors. Their owner is
// const and their public contract has no mutation or peer-execution capability.
struct NativeAwayReadActors final : AwayViews
{
	struct Ship final : AwayShipView
	{
		const Runtime &owner;
		EntityId id;
		Ship(const Runtime &owner, EntityId id);
		~Ship() override;
		AwayTransportLayout layout() const override;
		const PersonnelState &personnel() const override;
		bool team_present(const std::string &key) const override;
		const CombatMechanism &team(const std::string &key) const override;
		const DockConnection *dock_connection() const override;
	};
	struct Craft final : AwayCraftView
	{
		const Runtime &owner;
		EntityId id;
		Craft(const Runtime &owner, EntityId id);
		~Craft() override;
		const CraftState &state() const override;
	};
	const Runtime &owner;
	mutable std::map<EntityId, Ship> ships;
	mutable std::map<EntityId, Craft> crafts;
	explicit NativeAwayReadActors(const Runtime &owner);
	~NativeAwayReadActors() override;
	const AwayShipView &ship(EntityId id) const override;
	const AwayCraftView &craft(EntityId id) const override;
	bool is_craft(EntityId id) const override;
	EntityId transport(EntityId ship, const std::string &key) const override;
};
struct NativeAwayReadBinding
{
	NativeAwayReadActors actors;
	AwayObservation observation;
	explicit NativeAwayReadBinding(const Runtime &owner);
	~NativeAwayReadBinding();
	NativeAwayReadBinding(const NativeAwayReadBinding &) = delete;
	NativeAwayReadBinding &operator=(const NativeAwayReadBinding &) = delete;
	NativeAwayReadBinding(NativeAwayReadBinding &&) = delete;
	NativeAwayReadBinding &operator=(NativeAwayReadBinding &&) = delete;
};

// Execution frames own their actor caches and peer adapters, then bind the
// context to those stable members. Frames cannot be copied or moved.
struct NativeAwayBinding
{
	NativeAwayActors actors;
	NativeAwayPeers peers;
	AwayExecution execution;
	explicit NativeAwayBinding(Runtime &owner);
	~NativeAwayBinding();
	NativeAwayBinding(const NativeAwayBinding &) = delete;
	NativeAwayBinding &operator=(const NativeAwayBinding &) = delete;
	NativeAwayBinding(NativeAwayBinding &&) = delete;
	NativeAwayBinding &operator=(NativeAwayBinding &&) = delete;
};

struct NativeAwayRestoreChecks final : AwayRestoreChecks
{
	const Runtime &owner;
	explicit NativeAwayRestoreChecks(const Runtime &owner);
	~NativeAwayRestoreChecks() override;
	void boarding(const AwayDefinition &definition, const AwayMission &mission) const override;
};
struct NativeAwayPersistenceBinding
{
	NativeAwayReadBinding read;
	NativeAwayRestoreChecks checks;
	AwayRestoreContext context;
	explicit NativeAwayPersistenceBinding(const Runtime &owner);
	~NativeAwayPersistenceBinding();
	NativeAwayPersistenceBinding(const NativeAwayPersistenceBinding &) = delete;
	NativeAwayPersistenceBinding &operator=(const NativeAwayPersistenceBinding &) = delete;
	NativeAwayPersistenceBinding(NativeAwayPersistenceBinding &&) = delete;
	NativeAwayPersistenceBinding &operator=(NativeAwayPersistenceBinding &&) = delete;
};
struct NativeAwayProjectionSource final : AwayProjectionSource
{
	const Runtime &owner;
	explicit NativeAwayProjectionSource(const Runtime &owner);
	~NativeAwayProjectionSource() override;
	AwayPermittedRow permitted_row(std::size_t mission_index, const ConsoleAddress &viewer) const override;
};
} // namespace phoenix::world_detail
