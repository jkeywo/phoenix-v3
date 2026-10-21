// Included inside World::Impl. Personnel commands use Station ownership, never role names.
const PersonnelDuty *personnel_duty(EntityId id, const std::string &duty) const
{
	const auto &d = definition(id).personnel;
	const auto found = d.duty_indices.find(duty);
	return found == d.duty_indices.end() ? nullptr : &d.duties[found->second];
}

OfficerState personnel_destination(EntityId id, const PersonnelDuty &duty) const
{
	const auto &d = definition(id).personnel;
	OfficerState out;
	out.location = std::find(d.compartments.begin(), d.compartments.end(), duty.compartment) - d.compartments.begin();
	if (!duty.team.empty())
		for (auto child : entities.at(id).get<EquipmentIndex>().values)
			if (child.get<Equipment>().id == duty.team)
			{
				const auto &r = child.get<CombatMechanism>().get<RepairState>();
				out.location = r.location;
				out.edge_to = r.edge_to;
				out.remaining = r.travel;
				break;
			}
	return out;
}
bool personnel_owns(const ConsoleAddress &address, const std::string &department) const
{
	const auto *s = seat(address.ship, address.console);
	for (const auto &d : definition(address.ship).personnel.departments)
		if (d.id == department)
			return s && s->station == d.station;
	return false;
}
void advance_personnel(EntityId id)
{
	auto entity = entities.at(id);
	auto &state = entity.get_mut<PersonnelState>();
	const auto &d = definition(id).personnel;
	bool arrived = false;
	for (auto &o : state.officers)
	{
		const auto *duty = personnel_duty(id, o.duty);
		if (o.present && duty)
		{
			const auto at = personnel_destination(id, *duty);
			o.location = at.location;
			o.edge_to = at.edge_to;
			o.remaining = at.remaining;
			continue;
		}
		if (o.remaining > 0)
		{
			if (--o.remaining > 0)
				continue;
			o.location = static_cast<std::size_t>(o.edge_to);
			o.edge_to = -1;
		}
		if (!duty)
			continue;
		const auto target = personnel_destination(id, *duty);
		if (o.location == target.location && target.remaining == 0)
		{
			o.present = true;
			arrived = true;
			continue;
		}
		const auto route =
		    compartment_route(d.compartments.size(), d.links, o.location,
		                      target.remaining > 0 ? static_cast<std::size_t>(target.edge_to) : target.location);
		if (route.first >= 0)
		{
			o.edge_to = route.first;
			o.remaining = route.second;
		}
	}
	if (arrived)
		refresh_personnel_bonuses(state, d);
}
std::int64_t officer_bonus(EntityId id, const std::string &system, const std::string &slot) const
{
	const auto entity = entities.at(id);
	const auto *state = entity.try_get<PersonnelState>();
	if (!state)
		return 0;
	const auto b = state->bonuses.find({system, slot});
	return b == state->bonuses.end() ? 0 : b->second;
}
std::int64_t officer_value(EntityId id, const std::string &system, const std::string &slot, std::int64_t base,
                           bool inverse = false) const
{
	return personnel_scale(base, officer_bonus(id, system, slot), inverse);
}
std::pair<std::string, std::string> officer_speaker(EntityId id, const std::string &console,
                                                    const std::string &controller) const
{
	const auto &d = definition(id).personnel;
	const auto &state = entities.at(id).get<PersonnelState>();
	for (std::size_t i = 0; i < state.officers.size(); ++i)
		if (state.officers[i].present)
		{
			const auto *duty = personnel_duty(id, state.officers[i].duty);
			if (duty && duty->controller == controller && duty->console == console)
				return {d.officers[i].name, d.officers[i].portrait.empty() ? "generic-crew" : d.officers[i].portrait};
		}
	return {controller, "generic-crew"};
}
bool personnel_command(const ConsoleAddress &address, const OperatingCommand &command, bool execute)
{
	if (!command.controller.empty() || !command.channel.empty() || command.request_id || !command.arguments.empty() ||
	    command.value || !command.speaker_name.empty() || !command.speaker_avatar.empty())
		return false;
	const auto &d = definition(address.ship).personnel;
	auto &state = entities.at(address.ship).get_mut<PersonnelState>();
	if (command.generation != state.revision || state.revision >= 9007199254740991ULL)
		return false;
	const auto it = std::find_if(d.officers.begin(), d.officers.end(),
	                             [&](const auto &o)
	                             {
		                             return o.id == command.system;
	                             });
	if (it == d.officers.end() || !personnel_owns(address, it->department))
		return false;
	const auto i = static_cast<std::size_t>(it - d.officers.begin());
	if (away_reserved(address.ship, "officer:" + it->id))
		return false;
	const auto *duty = personnel_duty(address.ship, command.text);
	if (command.action == "personnel.relieve")
	{
		if (!command.text.empty())
			return false;
	}
	else if (command.action == "personnel.assign" || command.action == "personnel.swap")
	{
		if (!duty || duty->department != it->department ||
		    (!duty->team.empty() && away_reserved(address.ship, "team:" + duty->team)) ||
		    (!duty->required_trait.empty() &&
		     std::find(it->traits.begin(), it->traits.end(), duty->required_trait) == it->traits.end()))
			return false;
	}
	else
		return false;
	std::size_t occupied = state.officers.size();
	if (duty)
		for (std::size_t j = 0; j < state.officers.size(); ++j)
			if (state.officers[j].duty == duty->id)
				occupied = j;
	if (occupied == i)
		return false;
	if (command.action == "personnel.swap" && (occupied == state.officers.size() || state.officers[i].duty.empty()))
		return false;
	if (command.action == "personnel.swap")
	{
		const auto *previous = personnel_duty(address.ship, state.officers[i].duty);
		if (!previous || (!previous->required_trait.empty() &&
		                  std::find(d.officers[occupied].traits.begin(), d.officers[occupied].traits.end(),
		                            previous->required_trait) == d.officers[occupied].traits.end()))
			return false;
	}
	if (duty)
	{
		const auto &o = state.officers[i];
		const auto target = personnel_destination(address.ship, *duty);
		const auto from = o.remaining ? static_cast<std::size_t>(o.edge_to) : o.location;
		if (from != target.location &&
		    compartment_route(d.compartments.size(), d.links, from, target.location).first < 0)
			return false;
	}
	if (execute)
	{
		// Capture physical team position before detaching an accompanying officer.
		auto detach = [&](std::size_t index, const std::string &next)
		{
			auto &o = state.officers[index];
			if (o.present)
				if (const auto *old = personnel_duty(address.ship, o.duty))
				{
					const auto at = personnel_destination(address.ship, *old);
					o.location = at.location;
					o.edge_to = at.edge_to;
					o.remaining = at.remaining;
				}
			o.present = false;
			o.duty = next;
		};
		const auto old = state.officers[i].duty;
		if (occupied < state.officers.size())
			detach(occupied, command.action == "personnel.swap" ? old : std::string{});
		detach(i, duty ? duty->id : std::string{});
		++state.revision;
		refresh_personnel_bonuses(state, d);
	}
	return true;
}
PersonnelState personnel_snapshot(EntityId id) const
{
	auto snapshot = entities.at(id).get<PersonnelState>();
	for (auto &o : snapshot.officers)
		if (o.present)
			if (const auto *duty = personnel_duty(id, o.duty))
			{
				const auto at = personnel_destination(id, *duty);
				o.location = at.location;
				o.edge_to = at.edge_to;
				o.remaining = at.remaining;
			}
	return snapshot;
}
Json personnel_board(const ConsoleAddress &address) const
{
	const auto &d = definition(address.ship).personnel;
	const auto state = personnel_snapshot(address.ship);
	Json out{{"revision", state.revision},
	         {"departments", Json::array()},
	         {"duties", Json::array()},
	         {"officers", Json::array()}};
	for (const auto &dep : d.departments)
		if (personnel_owns(address, dep.id))
			out["departments"].push_back({{"id", dep.id}, {"label", dep.label}});
	for (const auto &duty : d.duties)
		if (personnel_owns(address, duty.department))
		{
			Json effects = Json::array();
			for (const auto &[system, slots] : duty.effects)
				for (const auto &slot : slots)
					effects.push_back({{"system", system},
					                   {"slot", slot},
					                   {"multiplier", officer_value(address.ship, system, slot, 1000)}});
			const auto at = personnel_destination(address.ship, duty);
			out["duties"].push_back({{"id", duty.id},
			                         {"label", duty.label},
			                         {"department", duty.department},
			                         {"location", d.compartments.at(at.location)},
			                         {"effects", effects}});
		}
	for (std::size_t i = 0; i < d.officers.size(); ++i)
		if (personnel_owns(address, d.officers[i].department))
		{
			const auto &o = d.officers[i];
			const auto &r = state.officers[i];
			Json traits = Json::array();
			for (const auto &key : o.traits)
			{
				const auto &t = d.traits.at(key);
				traits.push_back({{"label", t.label}, {"bonuses", t.bonuses}});
			}
			out["officers"].push_back({{"id", o.id},
			                           {"name", o.name},
			                           {"portrait", o.portrait},
			                           {"department", o.department},
			                           {"duty", r.duty},
			                           {"present", r.present},
			                           {"location", away_officer_location(address.ship, o.id).empty()
			                                            ? d.compartments.at(r.location)
			                                            : away_officer_location(address.ship, o.id)},
			                           {"remaining", r.remaining},
			                           {"destination", r.edge_to < 0 ? std::string{} : d.compartments.at(r.edge_to)},
			                           {"traits", traits}});
		}
	return out;
}
void restore_personnel(EntityId id, const Json &saved)
{
	auto state = saved.get<PersonnelState>();
	const auto &d = definition(id).personnel;
	if (state.officers.size() != d.officers.size() || !state.revision || state.revision > 9007199254740991ULL)
		throw std::runtime_error("Invalid personnel save");
	std::set<std::string> duties;
	for (std::size_t i = 0; i < state.officers.size(); ++i)
	{
		const auto &o = state.officers[i];
		const auto *duty = personnel_duty(id, o.duty);
		if (duty && !duty->required_trait.empty() &&
		    std::find(d.officers[i].traits.begin(), d.officers[i].traits.end(), duty->required_trait) ==
		        d.officers[i].traits.end())
			throw std::runtime_error("Unqualified saved officer assignment");
		if (o.location >= d.compartments.size() || o.edge_to < -1 ||
		    o.edge_to >= static_cast<std::int64_t>(d.compartments.size()) || o.remaining < 0 || o.remaining > 36000 ||
		    ((o.edge_to >= 0) != (o.remaining > 0)) ||
		    (!o.duty.empty() &&
		     (!duty || duty->department != d.officers[i].department || !duties.insert(o.duty).second)) ||
		    (o.present && !duty))
			throw std::runtime_error("Invalid saved officer assignment/travel");
		if (o.remaining)
		{
			bool found = false;
			for (const auto &l : d.links)
				if (((l.from == o.location && l.to == o.edge_to) || (l.to == o.location && l.from == o.edge_to)) &&
				    o.remaining <= l.ticks)
					found = true;
			if (!found)
				throw std::runtime_error("Invalid saved officer route");
		}
		if (o.present && duty->team.empty() && (o.remaining || d.compartments.at(o.location) != duty->compartment))
			throw std::runtime_error("Officer present outside duty compartment");
	}
	refresh_personnel_bonuses(state, d);
	entities.at(id).set<PersonnelState>(state);
}
