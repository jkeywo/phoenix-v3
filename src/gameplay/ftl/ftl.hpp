// World::Impl FTL mechanism and per-crew departure boundary.
DepartureCoordinator departures;
bool departure_member_authorised(EntityId leader, EntityId member) const
{
	if (operational_owner(leader) == operational_owner(member))
		return true;
	for (std::size_t i = 0; i < interdictions.size(); ++i)
	{
		const auto &operation = interdictions[i];
		const auto target = interdiction_definitions[i].target;
		const auto pirate = [&](EntityId id)
		{
			return std::find(operation.ships.begin(), operation.ships.end(), id) != operation.ships.end();
		};
		if (operation.captured && pirate(operational_owner(target)) && (leader == target || pirate(leader)) &&
		    (member == target || pirate(member)))
			return true;
	}
	return false;
}
// Older saves could retain the pirate-only remainder after the prize changed owner.
// Discard only that identifiable obsolete encounter goal, without changing physical state.
void discard_obsolete_encounter_departures()
{
	std::vector<EntityId> obsolete_groups;
	for (const auto &[leader, group] : departures.groups())
	{
		bool obsolete = false;
		for (std::size_t i = 0; i < interdictions.size(); ++i)
		{
			const auto &operation = interdictions[i];
			const auto participant = [&](EntityId id)
			{
				return std::find(operation.ships.begin(), operation.ships.end(), id) != operation.ships.end();
			};
			if (operation.captured && !participant(operational_owner(interdiction_definitions[i].target)) &&
			    participant(leader) && std::all_of(group.begin(), group.end(), participant))
			{
				obsolete = true;
				break;
			}
		}
		if (obsolete)
			obsolete_groups.push_back(leader);
	}
	for (const auto leader : obsolete_groups)
		departures.erase_group(leader, true);
}
void assign_departure_group(EntityId leader, const std::set<EntityId> &members)
{
	departures.assign(leader, members);
}
void prune_departure_groups()
{
	departures.prune({[this](EntityId id)
	                  {
		                  return truly_destroyed(id) || departed(id);
	                  },
	                  [this](EntityId id, EntityId leader)
	                  {
		                  return entities.at(id).get<FactionMembership>().id ==
		                         entities.at(leader).get<FactionMembership>().id;
	                  }});
}
EntityId departure_leader(EntityId member) const
{
	return departures.leader(member,
	                         [this](EntityId leader)
	                         {
		                         return !truly_destroyed(leader) && !departed(leader);
	                         });
}

flecs::entity departure_drive(EntityId ship) const
{
	for (auto child : entities.at(ship).get<EquipmentIndex>().values)
		if (child.has<FtlReserve>())
			return child;
	return {};
}
std::string departure_group_blocker(EntityId ship) const
{
	const auto leader = departure_leader(ship);
	if (!departures.groups().contains(leader))
		return {};
	for (const auto member : departures.groups().at(leader))
		if (!truly_destroyed(member) && !departed(member))
		{
			if (!departure_member_authorised(leader, member))
				return "Departure group ownership changed";
			if (member != leader && ftl_local_facts(member).second)
				return "Friendly visitors await recovery";
			const auto drive = departure_drive(member);
			if (!drive || !ftl_blocker(member, drive).empty())
				return "Departure group not ready";
		}
	return {};
}
bool departed(EntityId id) const
{
	return entities.contains(id) && entities.at(id).has<MissionDeparture>() &&
	       entities.at(id).get<MissionDeparture>().report.value("reason", "ftl") == "ftl";
}
ConsoleAddress ftl_owner(EntityId id, const std::string &system) const
{
	const auto &d = definition(id);
	if (!d.ftl.contains(system) || !d.layout)
		return {};
	for (const auto &ctrl : d.layout->controllers)
		for (const auto &channel : ctrl.commands)
			if (channel.action == OperatingAction::SystemEnabled)
				for (const auto i : channel.consumers)
					if (d.fitted->values.at(i).id == system)
						return {id, ctrl.console};
	return {};
}
void retire_departed(EntityId id)
{
	retire_ship_processes(id);
	auto ship = entities.at(id);
	ship.disable();
	for (auto child : ship.get<EquipmentIndex>().values)
		child.disable();
	displays.erase(id);
}
std::vector<std::string> ftl_left_behind(EntityId id) const
{
	std::vector<std::string> names;
	for (const auto &[asset, disposition] : personnel_dispositions(id))
		if (disposition == "deployed" || disposition == "freed")
			names.push_back(away_asset_label(id, asset));
	for (const auto &[craft, e] : craft_rows())
		if (e.get<CraftState>().home == id && !craft_dormant(e))
			names.push_back(e.get<Definition>().name + " #" + std::to_string(craft));
	return names;
}
std::string ftl_blocker(EntityId id, flecs::entity child) const
{
	if (departed(id))
		return "Departed via FTL";
	if (dock_busy(id))
		return "Undock before FTL departure";
	const auto ship = entities.at(id);
	const auto &fit = child.get<Equipment>();
	bool displacement = false;
	for (std::size_t i = 0; i < away_missions.size(); ++i)
		if ((away_ship(away_definitions[i]) == id || away_definitions[i].target == id) &&
		    away_ship(away_definitions[i]) &&
		    definition(away_ship(away_definitions[i])).displacements.contains(away_missions[i].transport) &&
		    away_missions[i].transfer > 0 &&
		    (away_missions[i].phase == AwayPhase::Outbound || away_missions[i].phase == AwayPhase::Inbound))
		{
			displacement = true;
			break;
		}
	return ftl_reserve_blocker(
	    child.get<FtlReserve>(), ship.get<ShipResources>(), child.get<ResourceBinding>().slot,
	    {fit.enabled, structurally_disabled(ship, fit.id), combat_dead(ship), fault_effectiveness(ship, fit.id), 0},
	    displacement);
}
void prepare_ftl(flecs::entity ship)
{
	auto &power = ship.get_mut<ShipResources>();
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *ftl = child.try_get<FtlReserve>())
		{
			const auto entity = ship.get<StableId>().id;
			const auto &equipment = child.get<Equipment>().id;
			prepare_ftl_reserve(*ftl, power, child.get<ResourceBinding>().slot,
			                    {child.get<Equipment>().enabled, structurally_disabled(ship, equipment),
			                     combat_dead(ship), fault_effectiveness(ship, equipment),
			                     native_process_rate(entity, "ftl", equipment)});
		}
}
void actuate_ftl(flecs::entity ship)
{
	auto &power = ship.get_mut<ShipResources>();
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *ftl = child.try_get_mut<FtlReserve>())
			actuate_ftl_reserve(*ftl, power.network, rate);
}
// Called after the paid machinery receipts. The finite completion is an event;
// readiness continues to come from the live reserve and never replaces it.
void finalise_ftl_processes()
{
	for (const auto &[id, ship] : entities)
		if (ship.has<ShipResources>())
			for (auto child : ship.get<EquipmentIndex>().values)
				if (const auto *ftl = child.try_get<FtlReserve>())
				{
					const auto &equipment = child.get<Equipment>().id;
					const auto &network = ship.get<ShipResources>().network;
					const auto energy = network.stores().at(ftl->definition.store).energy();
					const auto capacity = network.definition().stores.at(ftl->definition.store).capacity;
					const auto address = custom_property_address(RuleActor{id, equipment});
					auto *runtime = process_runtime(address);
					if (!runtime)
						continue;
					std::vector<std::string> processes;
					for (const auto &[key, state] : runtime->processes)
						if (process_definition(state).native == NativeProcessKind::Ftl && state.native_id == equipment)
							processes.push_back(key);
					for (const auto &key : processes)
					{
						auto *state = process_state(address, key);
						if (!state)
							continue;
						const bool ready = state->requested && state->admitted && child.get<Equipment>().enabled &&
						                   !combat_dead(ship) && !departed(id) && energy == capacity &&
						                   network.operation_ready(ftl->definition.store);
						if (state->completed && energy < capacity && state->requested)
						{
							// A genuinely depleted/unavailable reserve begins another preparation
							// episode; it must earn readiness and its completion again.
							process_request(address, key, true);
							state = process_state(address, key);
						}
						state->target = "ftl:" + equipment;
						state->origin.target = state->origin.subject = RuleActor{id, equipment};
						state->progress = energy;
						state->required_work = capacity;
						state->ready = ready;
						if (ready && !state->completed)
						{
							const auto generation = state->generation, run = state->run;
							commit_finite_process_completion(address, key, state->target);
							// Completion consequences can spend or alter the reserve, or
							// remove this run. Publish its resulting physical facts now;
							// another demand episode is considered on a later tick.
							state = process_state(address, key);
							if (!state || state->generation != generation || state->run != run)
								continue;
							const auto &actual = ship.get<ShipResources>().network;
							state->progress = actual.stores().at(ftl->definition.store).energy();
							state->required_work = actual.definition().stores.at(ftl->definition.store).capacity;
							state->ready = state->requested && state->admitted && child.get<Equipment>().enabled &&
							               !combat_dead(ship) && !departed(id) &&
							               state->progress == state->required_work &&
							               actual.operation_ready(ftl->definition.store);
						}
					}
				}
}
std::pair<bool, std::int64_t> ftl_local_facts(EntityId id) const
{
	bool completed = false;
	std::int64_t visitors = 0;
	for (std::size_t i = 0; i < away_missions.size(); ++i)
		if (away_definitions[i].target == id)
		{
			const auto &m = away_missions[i];
			completed |= m.outcome.starts_with("complete:");
			if (m.deployed() && !m.boarding.prize_crew)
				visitors += m.assignments.size() + m.accompanying.size();
			if (m.phase == AwayPhase::Outbound || m.phase == AwayPhase::Inbound)
				++visitors;
		}
	return {completed, visitors};
}
Json ftl_board(const ConsoleAddress &a) const
{
	Json rows = Json::array();
	const auto ship = entities.at(a.ship);
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *ftl = child.try_get<FtlReserve>(); ftl && ftl_owner(a.ship, child.get<Equipment>().id) == a)
		{
			const auto &p = ship.get<ShipResources>();
			const auto &s = p.network.stores().at(ftl->definition.store);
			auto blocker = ftl_blocker(a.ship, child);
			if (blocker.empty())
				blocker = departure_group_blocker(a.ship);
			rows.push_back({{"system", child.get<Equipment>().id},
			                {"energy", s.energy()},
			                {"capacity", p.definition.network.stores.at(ftl->definition.store).capacity},
			                {"enabled", child.get<Equipment>().enabled},
			                {"ready", blocker.empty()},
			                {"power_draw", p.network.telemetry().at(ftl->definition.store).accepted_incoming * rate},
			                {"heat", s.heat},
			                {"blocker", blocker},
			                {"left_behind", ftl_left_behind(a.ship)},
			                {"visitors", ftl_local_facts(a.ship).second},
			                {"work_completed", ftl_local_facts(a.ship).first}});
		}
	return rows;
}
bool ftl_command(const ConsoleAddress &a, const OperatingCommand &c, bool execute, bool ai = false)
{
	if (c.action != "ftl.depart" || departed(a.ship) || c.value || !c.channel.empty() || c.request_id ||
	    !c.arguments.empty() || !c.text.empty() || !c.speaker_name.empty() || !c.speaker_avatar.empty())
		return false;
	auto ship = entities.at(a.ship);
	if (ftl_owner(a.ship, c.system) != a)
		return false;
	bool authority = false;
	for (const auto &ctrl : ship.get<EntityControls>().controllers)
		if (ctrl.definition.console == a.console && ctrl.definition.id == c.controller && ctrl.human != ai &&
		    ctrl.generation == c.generation)
			for (const auto &channel : ctrl.definition.commands)
				for (const auto i : channel.consumers)
					authority |= ship.get<EquipmentIndex>().values.at(i).get<Equipment>().id == c.system;
	if (!authority)
		return false;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (child.has<FtlReserve>() && child.get<Equipment>().id == c.system)
		{
			if (!ftl_blocker(a.ship, child).empty() || !departure_group_blocker(a.ship).empty() ||
			    departure_leader(a.ship) != a.ship)
				return false;
			if (!execute)
				return true;
			departures.request(a, c, ai);
			return true;
		}
	return false;
}

void commit_departures()
{
	auto pending = departures.take_requests();
	std::sort(pending.begin(), pending.end(),
	          [](const auto &a, const auto &b)
	          {
		          return std::get<0>(a).ship < std::get<0>(b).ship;
	          });
	std::vector<std::pair<ConsoleAddress, OperatingCommand>> accepted;
	std::set<EntityId> seen;
	for (const auto &[a, c, ai] : pending)
		if (!seen.contains(a.ship) && ftl_command(a, c, false, ai))
		{
			accepted.emplace_back(a, c);
			seen.insert(a.ship);
		}
	if (accepted.empty())
		return;
	// Readiness and ownership are rechecked as one batch before any member leaves.
	const auto leaders = accepted;
	for (const auto &[a, command] : leaders)
		if (departures.groups().contains(a.ship))
			for (auto member : departures.groups().at(a.ship))
				if (member != a.ship && !truly_destroyed(member) && !departed(member) && !seen.contains(member))
				{
					const auto drive = departure_drive(member);
					OperatingCommand companion{member, "ftl.depart", 0, drive.get<Equipment>().id};
					accepted.emplace_back(ftl_owner(member, companion.system), companion);
					seen.insert(member);
				}
	for (const auto &[a, c] : accepted)
	{
		auto ship = entities.at(a.ship);
		for (auto child : ship.get<EquipmentIndex>().values)
			if (child.has<FtlReserve>() && child.get<Equipment>().id == c.system)
			{
				Json report{{"reason", "ftl"},
				            {"tick", ecs.get<Clock>().tick},
				            {"objectives", Json::array()},
				            {"left_behind", ftl_left_behind(a.ship)}};
				std::map<std::uint64_t, Json> objectives;
				for (const auto &r : knowledge.public_board(a.ship).value("shared", Json::array()))
					if (r.value("schema", "") == "phoenix.objective")
						objectives[r.at("fields").at("id").get<std::uint64_t>()] = r.at("fields");
				for (const auto &[id, o] : objectives)
					report["objectives"].push_back(
					    {{"title", o.value("title", "")}, {"status", o.value("status", "active")}});
				auto &p = ship.get_mut<ShipResources>();
				const auto store = child.get<FtlReserve>().definition.store;
				p.network.dissipate(store, p.network.stores().at(store).energy(), false);
				ship.set<MissionDeparture>({ftl_projection::report(report)});
				for (std::size_t i = 0; i < away_missions.size(); ++i)
					if (away_ship(away_definitions[i]) == a.ship)
					{
						auto &m = away_missions[i];
						if (!m.transport.starts_with("craft:") &&
						    (m.phase == AwayPhase::Outbound || m.phase == AwayPhase::Inbound))
						{
							m.phase = m.phase == AwayPhase::Inbound ? AwayPhase::Deployed : AwayPhase::Open;
							m.transfer = 0;
							++m.revision;
						}
					}
				retire_departed(a.ship);
			}
	}
	update_mission_facts();
	for (const auto &[a, c] : accepted)
	{
		auto ship = entities.at(a.ship);
		ship.set<MissionDeparture>(
		    {ftl_projection::report(authoritative_report(a.ship, ship.get<MissionDeparture>().report))});
		if (scenario)
			scenario->runtime().withdraw_actor(a.ship);
	}
}
