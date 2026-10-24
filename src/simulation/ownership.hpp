// Shared physical ownership and custody effects. No scenario identities belong here.
std::map<EntityId, std::map<std::string, PersonnelPosting>> personnel_postings;
bool captured_posting(EntityId ship, const std::string &asset) const
{
	return personnel_postings.contains(ship) && personnel_postings.at(ship).contains(asset) &&
	       personnel_postings.at(ship).at(asset).status == "captured";
}
void validate_personnel_postings() const
{
	for (const auto &[ship, postings] : personnel_postings)
	{
		if (!entities.contains(ship) || postings.size() > 256)
			throw std::runtime_error("Invalid personnel posting owner");
		for (const auto &[asset, posting] : postings)
		{
			if (!entities.contains(posting.target) || posting.target == ship ||
			    (posting.status != "captured" && posting.status != "freed" && posting.status != "prize_crew"))
				throw std::runtime_error("Invalid personnel posting");
			bool valid = false, detained = false;
			if (asset.starts_with("team:"))
			{
				const auto team = away_equipment(ship, asset.substr(5));
				if (team && team.get<Equipment>().kind == OperatingSystem::Team)
				{
					valid = true;
					detained = team.get<CombatMechanism>().get<TeamState>().detained;
				}
			}
			for (std::size_t n = 0; n < definition(ship).personnel.officers.size(); ++n)
				if (asset == "officer:" + definition(ship).personnel.officers[n].id)
				{
					valid = true;
					detained = entities.at(ship).get<PersonnelState>().officers[n].detained;
				}
			if (!valid || detained != (posting.status == "captured"))
				throw std::runtime_error("Invalid custody identity or detention");
			if (posting.status == "prize_crew" && operational_owner(posting.target) != operational_owner(ship))
				throw std::runtime_error("Prize posting without ownership");
		}
	}
}
void set_operational_owner(EntityId target_id, EntityId origin_id)
{
	const auto target = entities.at(target_id);
	for (auto it = departure_groups.begin(); it != departure_groups.end();)
	{
		if (it->first == target_id)
			it = departure_groups.erase(it);
		else
		{
			it->second.erase(target_id);
			++it;
		}
	}
	departure_groups[operational_owner(origin_id)].insert(operational_owner(origin_id));
	departure_groups[operational_owner(origin_id)].insert(target_id);
	captured_by[target_id] = operational_owner(origin_id);
	target.set<FactionMembership>({entities.at(origin_id).get<FactionMembership>().id});
	for (const auto &[id, craft] : craft_rows())
		if (craft.get<CraftState>().home == target_id)
			craft.set<FactionMembership>(target.get<FactionMembership>());
	std::vector<std::uint64_t> revoke;
	for (const auto &[claim, address] : claims)
		if (address.ship == target_id)
			revoke.push_back(claim);
	for (auto claim : revoke)
		claims.erase(claim);
	if (target.has<EntityControls>())
		refresh_controllers(target_id);
	prune_displays();
	if (target.has<EntityControls>())
		for (auto &controller : target.get_mut<EntityControls>().controllers)
		{
			++controller.generation;
			controller.human = false;
			controller.operator_claimant = 0;
			controller.action = {};
			controller.incoming_requests.clear();
			controller.outgoing_requests.clear();
			for (auto &command : controller.commands)
			{
				command.request_id = 0;
				command.consumed = command.revision;
			}
			if (controller.brain)
				controller.brain->continuation = {};
		}
	for (auto child : target.get<EquipmentIndex>().values)
		if (child.has<DirectedRuntime>())
			directed_setting(target_id, child.get<Equipment>().id, "target", 0, true);
		else if (auto *mechanism = child.try_get_mut<CombatMechanism>();
		         mechanism && (mechanism->definition.kind == OperatingSystem::Weapon ||
		                       mechanism->definition.kind == OperatingSystem::Turret))
			mechanism->target() = 0;
	// Original personnel are not automatically recruited into the prize crew.
	for (auto child : target.get<EquipmentIndex>().values)
		if (auto *mechanism = child.try_get_mut<CombatMechanism>();
		    mechanism && mechanism->definition.kind == OperatingSystem::Team)
			mechanism->get<TeamState>().detained = mechanism->get<TeamState>().condition != "lost";
	if (target.has<PersonnelState>())
	{
		for (auto &officer : target.get_mut<PersonnelState>().officers)
		{
			officer.duty.clear();
			officer.present = false;
			officer.detained = officer.condition != "dead";
		}
		personnel_changed(target_id);
	}
}
void detained_asset(EntityId ship, const std::string &asset, bool detained)
{
	if (asset.starts_with("team:"))
		away_equipment(ship, asset.substr(5)).get_mut<CombatMechanism>().get<TeamState>().detained = detained;
	else if (asset.starts_with("officer:"))
	{
		auto &state = entities.at(ship).get_mut<PersonnelState>();
		const auto &officers = definition(ship).personnel.officers;
		for (std::size_t i = 0; i < officers.size(); ++i)
			if (officers[i].id == asset.substr(8))
				state.officers[i].detained = detained;
		++state.revision;
	}
}
void capture_visitors(EntityId target, EntityId captor)
{
	for (std::size_t i = 0; i < away_missions.size(); ++i)
	{
		auto &m = away_missions[i];
		const auto ship = away_ship(away_definitions[i]);
		if (away_definitions[i].target != target || !m.deployed() ||
		    operational_owner(ship) == operational_owner(captor))
			continue;
		for (const auto &[slot, asset] : m.assignments)
		{
			if (asset.starts_with("team:") &&
			    away_equipment(ship, asset.substr(5)).get<CombatMechanism>().get<TeamState>().condition == "lost")
				continue;
			if (mission_ledger.lost[ship].contains(asset))
				continue;
			if (asset.starts_with("officer:"))
			{
				const auto &pd = definition(ship).personnel;
				const auto officer = std::find_if(pd.officers.begin(), pd.officers.end(),
				                                  [&](const auto &o)
				                                  {
					                                  return asset == "officer:" + o.id;
				                                  });
				if (officer == pd.officers.end() ||
				    entities.at(ship).get<PersonnelState>().officers[officer - pd.officers.begin()].condition == "dead")
					continue;
			}
			personnel_postings[ship][asset] = {target, "captured"};
			detained_asset(ship, asset, true);
		}
		for (const auto &officer : m.accompanying)
		{
			const auto &pd = definition(ship).personnel;
			const auto it = std::find_if(pd.officers.begin(), pd.officers.end(),
			                             [&](const auto &o)
			                             {
				                             return o.id == officer;
			                             });
			if (it == pd.officers.end() ||
			    entities.at(ship).get<PersonnelState>().officers[it - pd.officers.begin()].condition == "dead")
				continue;
			personnel_postings[ship]["officer:" + officer] = {target, "captured"};
			detained_asset(ship, "officer:" + officer, true);
		}
		m.phase = AwayPhase::Captured;
		m.transfer = 0;
		m.care.clear();
		++m.revision;
		mission_transition("personnel.captured", ship, target, away_definitions[i].id, "captured");
	}
}
void free_captives(EntityId target, EntityId owner)
{
	for (auto &[ship, postings] : personnel_postings)
		if (operational_owner(ship) == owner)
			for (auto &[asset, posting] : postings)
				if (posting.target == target && posting.status == "captured")
				{
					posting.status = "freed";
					detained_asset(ship, asset, false);
					mission_transition("personnel.freed", ship, target, asset, "freed");
				}
	for (std::size_t i = 0; i < away_missions.size(); ++i)
		if (away_definitions[i].target == target && away_missions[i].phase == AwayPhase::Captured &&
		    operational_owner(away_ship(away_definitions[i])) == owner)
		{
			away_missions[i].phase = AwayPhase::Deployed;
			++away_missions[i].revision;
		}
}
void claim_away_target(std::size_t index, const std::string &capability)
{
	const auto &d = away_definitions[index];
	auto &m = away_missions[index];
	const auto ship = away_ship(d);
	if (!m.deployed() || !away_has_capability(ship, m, capability))
		throw std::runtime_error("Claim requires a deployed qualified team");
	set_operational_owner(d.target, ship);
	free_captives(d.target, operational_owner(ship));
	std::set<std::string> officers;
	for (auto it = m.assignments.begin(); it != m.assignments.end();)
		if (it->second.starts_with("team:") && away_equipment(ship, it->second.substr(5))
		                                           .get<CombatMechanism>()
		                                           .definition.get<TeamDefinition>()
		                                           .capabilities.contains(capability))
		{
			const auto team = it->second.substr(5);
			personnel_postings[ship][it->second] = {d.target, "prize_crew"};
			mission_transition("prize.assigned", ship, d.target, it->second, "prize_crew");
			if (const auto *duty = away_team_duty(ship, team))
			{
				const auto &pd = definition(ship).personnel;
				const auto &ps = entities.at(ship).get<PersonnelState>();
				for (std::size_t n = 0; n < pd.officers.size(); ++n)
					if (ps.officers[n].duty == duty->id && std::find(m.accompanying.begin(), m.accompanying.end(),
					                                                 pd.officers[n].id) != m.accompanying.end())
						officers.insert(pd.officers[n].id);
			}
			it = m.assignments.erase(it);
		}
		else
			++it;
	for (const auto &officer : officers)
	{
		personnel_postings[ship]["officer:" + officer] = {d.target, "prize_crew"};
		std::erase(m.accompanying, officer);
	}
	++m.revision;
	mission_transition("ship.claimed", ship, d.target, d.id, "claimed");
}
