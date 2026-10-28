// Private World adapter. Include once in its native Away implementation unit.
// Actor wrappers contain only a logical ID and private owner reference. Every
// component lookup occurs lazily at the call site; wrappers cache no ECS pointers.
struct NativeAwayActors final : AwayActors
{
	struct Ship final : AwayShipActor
	{
		Impl &owner;
		EntityId id;
		inline Ship(Impl &owner, EntityId id) : owner(owner), id(id)
		{
		}
		inline ~Ship() override = default;
		inline AwayTransportLayout layout() const override
		{
			const auto &d = owner.definition(id);
			return {d.personnel,     d.transport, d.craft_bays,
			        d.displacements, d.docking,   d.combat ? &*d.combat : nullptr};
		}
		inline PersonnelState &personnel() override
		{
			return owner.entities.at(id).get_mut<PersonnelState>();
		}
		inline const PersonnelState &personnel() const override
		{
			return owner.entities.at(id).get<PersonnelState>();
		}
		inline CombatMechanism &team(const std::string &key) override
		{
			return owner.away_equipment(id, key).get_mut<CombatMechanism>();
		}
		inline const CombatMechanism &team(const std::string &key) const override
		{
			return owner.away_equipment(id, key).get<CombatMechanism>();
		}
		inline const CombatStructure &structure() override
		{
			return owner.entities.at(id).get<CombatStructure>();
		}
		inline std::optional<AwayTeamMotion> team_motion(const std::string &key) override
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
		inline AwayTeamRoutes team_routes() override
		{
			const auto &structure = owner.entities.at(id).get<CombatStructure>();
			AwayTeamRoutes result{structure.compartments.size(), {}};
			for (const auto &edge : structure.definition.connections)
				if (owner.physical_edge_open(id, edge.from, edge.to))
					result.open_links.push_back(edge);
			return result;
		}
		inline AwayPersonnelRoutes personnel_routes() override
		{
			const auto &personnel = owner.definition(id).personnel;
			AwayPersonnelRoutes result{personnel.compartments.size(), {}};
			for (const auto &edge : personnel.links)
				if (owner.physical_edge_open(id, edge.from, edge.to))
					result.open_links.push_back(edge);
			return result;
		}
		inline const DockConnection *dock_connection() const override
		{
			return owner.entities.at(id).try_get<DockConnection>();
		}
		inline bool departed() override
		{
			return owner.departed(id);
		}
		inline bool dead() override
		{
			return owner.combat_dead(owner.entities.at(id));
		}
		inline bool dock_ready(const AwayDefinition &definition) override
		{
			return owner.away_dock_ready(id, definition);
		}
		inline bool system_operational(const std::string &system) override
		{
			return owner.away_equipment(id, system).get<Equipment>().enabled &&
			       !owner.structurally_disabled(owner.entities.at(id), system) &&
			       owner.fault_effectiveness(owner.entities.at(id), system) > 0;
		}
		inline std::int64_t officer_repair_work(const std::string &mission, const std::string &officer) override
		{
			return owner.effective_property(owner.entities.at(id), "away:" + mission + ":officer:" + officer,
			                                PropertyKey::repair, 1000);
		}
		inline std::int64_t structural_work(const std::vector<std::string> &teams, EntityId target,
		                                    std::int64_t compartment, const std::string &key, std::int64_t progress,
		                                    std::int64_t required) override
		{
			std::vector<flecs::entity> actors;
			for (const auto &team : teams)
				actors.push_back(owner.away_equipment(id, team));
			return owner.structural_repair_work(owner.entities.at(id), std::move(actors), target, compartment, key,
			                                    progress, required);
		}
	};
	struct Craft final : AwayCraftActor
	{
		Impl &owner;
		EntityId id;
		inline Craft(Impl &owner, EntityId id) : owner(owner), id(id)
		{
		}
		inline ~Craft() override = default;
		inline CraftState &state() override
		{
			return owner.entities.at(id).get_mut<CraftState>();
		}
		inline const CraftState &state() const override
		{
			return owner.entities.at(id).get<CraftState>();
		}
		inline bool dormant() override
		{
			return owner.craft_dormant(owner.entities.at(id));
		}
		inline bool available(const std::string &mission) override
		{
			return owner.craft_available(id, mission);
		}
		inline void launch() override
		{
			owner.launch_craft(owner.entities.at(id));
		}
		inline void return_home() override
		{
			owner.craft_return(owner.entities.at(id));
		}
	};
	Impl &owner;
	mutable std::map<EntityId, Ship> ships;
	mutable std::map<EntityId, Craft> crafts;
	inline explicit NativeAwayActors(Impl &owner) : owner(owner)
	{
	}
	inline ~NativeAwayActors() override = default;
	inline AwayShipActor &ship(EntityId id) override
	{
		return ships.try_emplace(id, owner, id).first->second;
	}
	inline AwayCraftActor &craft(EntityId id) override
	{
		return crafts.try_emplace(id, owner, id).first->second;
	}
	inline const AwayShipActor &ship(EntityId id) const override
	{
		return ships.try_emplace(id, owner, id).first->second;
	}
	inline const AwayCraftActor &craft(EntityId id) const override
	{
		return crafts.try_emplace(id, owner, id).first->second;
	}
	inline EntityId transport(EntityId ship, const std::string &key) const override
	{
		return owner.away_transport_entity(ship, key);
	}
	inline bool contains(EntityId id) override
	{
		return owner.entities.contains(id);
	}
	inline EntityId transport(EntityId ship, const std::string &key) override
	{
		return owner.away_transport_entity(ship, key);
	}
	inline std::int64_t distance(EntityId left, EntityId right) override
	{
		return owner.distance_3d(left, right);
	}
};
struct NativeAwayPeers final : AwayPeers
{
	Impl &owner;
	inline explicit NativeAwayPeers(Impl &owner) : owner(owner)
	{
	}
	inline ~NativeAwayPeers() override = default;
	inline ConversationId conversation(const AwayDefinition &definition) override
	{
		return owner.away_conversation(definition);
	}
	inline void prepare_contributions(std::size_t index) override
	{
		owner.prepare_away_tick_contributions(index);
	}
	inline bool assisted_send(std::size_t index) override
	{
		return owner.prepare_away_assisted_send(index);
	}
	inline bool boarding_visible(EntityId origin, EntityId target) override
	{
		return owner.boarding_visible(origin, target);
	}
	inline void advance_supply(std::size_t index) override
	{
		owner.advance_supply_mission(index);
	}
	inline void advance_craft_transport(std::size_t index, EntityId craft) override
	{
		owner.advance_craft_transport(index, craft);
	}
	inline void advance_boarding(std::size_t index) override
	{
		owner.advance_boarding(index);
	}
	inline void advance_medical(std::size_t index) override
	{
		owner.advance_away_medical(index);
	}
	inline bool boarding_action(std::size_t index, const std::string &action, const std::string &argument) override
	{
		return owner.boarding_action(index, action, argument);
	}
	inline bool has_capability(EntityId ship, const AwayMission &mission, const std::string &capability) override
	{
		return owner.away_has_capability(ship, mission, capability);
	}
	inline std::string blocker(std::size_t index) override
	{
		return owner.away_blocker(index);
	}
	inline bool automatic_repair(EntityId ship, const std::string &team) override
	{
		return owner.station_stars(owner.enabled_system_owner(ship, team)) < 3;
	}
	inline void record_insertion(EntityId origin, EntityId target) override
	{
		owner.record_interdiction_insertion(origin, target);
	}
	inline void claim_target(std::size_t index, const std::string &claim) override
	{
		owner.claim_away_target(index, claim);
	}
	inline void execute_effect(const std::string &group, const std::string &identity,
	                           const NativeRuleContext &context) override
	{
		owner.execute_tree_effect(group, identity, context);
	}
	inline void mission_transition(const std::string &kind, EntityId source, EntityId subject,
	                               const std::string &operation, const std::string &detail) override
	{
		owner.mission_transition(kind, source, subject, operation, detail);
	}
	inline void mission_change(const std::string &key, const std::string &value, const std::string &kind,
	                           EntityId source, EntityId subject, const std::string &operation,
	                           const std::string &detail) override
	{
		owner.mission_change(key, value, kind, source, subject, operation, detail);
	}
	inline void emit(EntityId ship, const std::string &event) override
	{
		owner.scenario->runtime().emit({owner.conversation_instance, ++owner.conversation_event_sequence, 0}, event,
		                               {{"ship", static_cast<std::int64_t>(ship)}});
	}
};

// Query actors are intentionally separate from execution actors. Their owner is
// const and their public contract has no mutation or peer-execution capability.
struct NativeAwayReadActors final : AwayViews
{
	struct Ship final : AwayShipView
	{
		const Impl &owner;
		EntityId id;
		inline Ship(const Impl &owner, EntityId id) : owner(owner), id(id)
		{
		}
		inline ~Ship() override = default;
		inline AwayTransportLayout layout() const override
		{
			const auto &d = owner.definition(id);
			return {d.personnel,     d.transport, d.craft_bays,
			        d.displacements, d.docking,   d.combat ? &*d.combat : nullptr};
		}
		inline const PersonnelState &personnel() const override
		{
			return owner.entities.at(id).get<PersonnelState>();
		}
		inline const CombatMechanism &team(const std::string &key) const override
		{
			return owner.away_equipment(id, key).get<CombatMechanism>();
		}
		inline const DockConnection *dock_connection() const override
		{
			return owner.entities.at(id).try_get<DockConnection>();
		}
	};
	struct Craft final : AwayCraftView
	{
		const Impl &owner;
		EntityId id;
		inline Craft(const Impl &owner, EntityId id) : owner(owner), id(id)
		{
		}
		inline ~Craft() override = default;
		inline const CraftState &state() const override
		{
			return owner.entities.at(id).get<CraftState>();
		}
	};
	const Impl &owner;
	mutable std::map<EntityId, Ship> ships;
	mutable std::map<EntityId, Craft> crafts;
	inline explicit NativeAwayReadActors(const Impl &owner) : owner(owner)
	{
	}
	inline ~NativeAwayReadActors() override = default;
	inline const AwayShipView &ship(EntityId id) const override
	{
		return ships.try_emplace(id, owner, id).first->second;
	}
	inline const AwayCraftView &craft(EntityId id) const override
	{
		return crafts.try_emplace(id, owner, id).first->second;
	}
	inline EntityId transport(EntityId ship, const std::string &key) const override
	{
		return owner.away_transport_entity(ship, key);
	}
};
struct NativeAwayReadBinding
{
	NativeAwayReadActors actors;
	AwayObservation observation;
	inline explicit NativeAwayReadBinding(const Impl &owner) : actors(owner), observation{actors, owner.slot_entities}
	{
	}
	inline ~NativeAwayReadBinding() = default;
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
	inline explicit NativeAwayBinding(Impl &owner)
	    : actors(owner), peers(owner), execution{actors,
	                                             peers,
	                                             owner.conversations,
	                                             owner.scenario ? &owner.scenario->runtime() : nullptr,
	                                             owner.slot_entities,
	                                             rate,
	                                             owner.personnel_postings}
	{
	}
	inline ~NativeAwayBinding() = default;
	NativeAwayBinding(const NativeAwayBinding &) = delete;
	NativeAwayBinding &operator=(const NativeAwayBinding &) = delete;
	NativeAwayBinding(NativeAwayBinding &&) = delete;
	NativeAwayBinding &operator=(NativeAwayBinding &&) = delete;
};
