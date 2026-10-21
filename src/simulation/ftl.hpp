// World::Impl FTL mechanism and per-crew departure boundary.
bool departed(EntityId id) const
{
	return entities.contains(id) && entities.at(id).has<MissionDeparture>();
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
	auto ship = entities.at(id);
	ship.disable();
	for (auto child : ship.get<EquipmentIndex>().values)
		child.disable();
	displays.erase(id);
}
std::vector<std::string> ftl_left_behind(EntityId id) const
{
	std::set<std::string> assets;
	for (std::size_t i = 0; i < away_missions.size(); ++i)
		if (away_ship(away_definitions[i]) == id && away_missions[i].deployed())
		{
			for (const auto &[slot, asset] : away_missions[i].assignments)
				assets.insert(asset);
			for (const auto &officer : away_missions[i].accompanying)
				assets.insert("officer:" + officer);
		}
	std::vector<std::string> names;
	for (const auto &asset : assets)
		names.push_back(away_asset_label(id, asset));
	return names;
}
std::string ftl_blocker(EntityId id, flecs::entity child) const
{
	if (departed(id))
		return "Departed via FTL";
	const auto ship = entities.at(id);
	const auto &fit = child.get<Equipment>();
	const auto &power = ship.get<ShipResources>();
	const auto store = child.get<FtlReserve>().definition.store;
	const auto &s = power.network.stores().at(store);
	const auto &reserve = power.definition.network.stores.at(store);
	if (!fit.enabled)
		return "FTL battery off";
	if (combat_dead(ship) || structurally_disabled(ship, fit.id) || !fault_effectiveness(ship, fit.id) || !s.enabled ||
	    s.thermal_stop || (reserve.stop_heat && s.heat >= reserve.stop_heat) ||
	    power.grants.at(child.get<ResourceBinding>().slot).blocked)
		return "FTL drive unavailable";
	for (std::size_t i = 0; i < away_missions.size(); ++i)
		if (away_ship(away_definitions[i]) == id && away_missions[i].transfer > 0 &&
		    (away_missions[i].phase == AwayPhase::Outbound || away_missions[i].phase == AwayPhase::Inbound))
			return "Displacement transfer active";
	if (s.energy != power.definition.network.stores.at(store).capacity)
		return "FTL battery charging";
	return {};
}
void prepare_ftl(flecs::entity ship)
{
	auto &power = ship.get_mut<ShipResources>();
	for (auto child : ship.get<EquipmentIndex>().values)
		if (const auto *ftl = child.try_get<FtlReserve>())
		{
			const auto &d = ftl->definition;
			const auto &s = power.network.stores().at(d.store);
			const bool enabled =
			    child.get<Equipment>().enabled && !structurally_disabled(ship, child.get<Equipment>().id) &&
			    fault_effectiveness(ship, child.get<Equipment>().id) > 0 && !s.thermal_stop && !combat_dead(ship);
			const bool full = s.energy == power.definition.network.stores.at(d.store).capacity;
			power.network.set_store_enabled(d.store, enabled);
			power.network.set_storage_limits(d.store, enabled ? d.charge_rate : 0,
			                                 power.definition.network.stores.at(d.store).discharge_rate);
			request_system_resource(power, child.get<ResourceBinding>().slot, 0, false,
			                        enabled ? (full ? d.ready_loss : d.charging_loss) : 0);
		}
}
void actuate_ftl(flecs::entity ship)
{
	auto &power = ship.get_mut<ShipResources>();
	for (auto child : ship.get<EquipmentIndex>().values)
		if (auto *ftl = child.try_get_mut<FtlReserve>())
		{
			const auto store = ftl->definition.store;
			if (!power.network.stores().at(store).enabled)
			{
				ftl->discharge_remainder += ftl->definition.off_discharge;
				power.network.dissipate(store, ftl->discharge_remainder / rate);
				ftl->discharge_remainder %= rate;
			}
		}
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
			const auto blocker = ftl_blocker(a.ship, child);
			rows.push_back({{"system", child.get<Equipment>().id},
			                {"energy", s.energy},
			                {"capacity", p.definition.network.stores.at(ftl->definition.store).capacity},
			                {"enabled", child.get<Equipment>().enabled},
			                {"ready", blocker.empty()},
			                {"power_draw", p.network.telemetry().at(ftl->definition.store).accepted_incoming * rate},
			                {"heat", s.heat},
			                {"blocker", blocker},
			                {"left_behind", ftl_left_behind(a.ship)}});
		}
	return rows;
}
bool ftl_command(const ConsoleAddress &a, const OperatingCommand &c, bool execute)
{
	if (c.action != "ftl.depart" || departed(a.ship) || c.value || !c.channel.empty() || c.request_id ||
	    !c.arguments.empty() || !c.text.empty() || !c.speaker_name.empty() || !c.speaker_avatar.empty())
		return false;
	auto ship = entities.at(a.ship);
	if (ftl_owner(a.ship, c.system) != a)
		return false;
	bool authority = false;
	for (const auto &ctrl : ship.get<EntityControls>().controllers)
		if (ctrl.definition.console == a.console && ctrl.definition.id == c.controller && ctrl.human &&
		    ctrl.generation == c.generation)
			for (const auto &channel : ctrl.definition.commands)
				for (const auto i : channel.consumers)
					authority |= ship.get<EquipmentIndex>().values.at(i).get<Equipment>().id == c.system;
	if (!authority)
		return false;
	for (auto child : ship.get<EquipmentIndex>().values)
		if (child.has<FtlReserve>() && child.get<Equipment>().id == c.system)
		{
			if (!ftl_blocker(a.ship, child).empty())
				return false;
			if (!execute)
				return true;
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
			p.network.dissipate(store, p.network.stores().at(store).energy, false);
			ship.set<MissionDeparture>({ftl_projection::report(report)});
			for (std::size_t i = 0; i < away_missions.size(); ++i)
				if (away_ship(away_definitions[i]) == a.ship)
				{
					auto &m = away_missions[i];
					if (m.phase == AwayPhase::Outbound || m.phase == AwayPhase::Inbound)
					{
						m.phase = m.phase == AwayPhase::Inbound ? AwayPhase::Deployed : AwayPhase::Open;
						m.transfer = 0;
						++m.revision;
					}
				}
			retire_departed(a.ship);
			if (scenario)
				scenario->runtime().withdraw_actor(a.ship);
			return true;
		}
	return false;
}
