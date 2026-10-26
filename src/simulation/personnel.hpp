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
				const auto &r = child.get<CombatMechanism>().get<TeamState>();
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
int station_stars(const ConsoleAddress &address) const
{
	const auto *controls = entities.at(address.ship).try_get<EntityControls>();
	if (!controls || !controls->layout.complexity_groups.contains(address.console))
		return 3;
	return controls->layout.profiles.at(controls->profiles.at(address.console)).stars;
}
bool manual_workload(const ConsoleAddress &address, const std::string &family) const
{
	const auto *controls = entities.at(address.ship).try_get<EntityControls>();
	if (!controls)
		return true;
	const auto &workloads = controls->layout.manual_workloads;
	return !workloads.contains(address.console) || !workloads.at(address.console).contains(family) ||
	       station_stars(address) >= workloads.at(address.console).at(family);
}
bool assisted_workload(const ConsoleAddress &address, const std::string &family) const
{
	const auto *controls = entities.at(address.ship).try_get<EntityControls>();
	if (!controls)
		return false;
	const auto &workloads = controls->layout.manual_workloads;
	return workloads.contains(address.console) && workloads.at(address.console).contains(family) &&
	       manual_workload(address, family) && console_operator(address) != 0;
}
void advance_personnel(EntityId id)
{
	auto entity = entities.at(id);
	auto &state = entity.get_mut<PersonnelState>();
	const auto &d = definition(id).personnel;
	bool arrived = false;
	for (const auto &officer : d.officers)
		if (!officer.duty.empty())
			if (const auto *duty = personnel_duty(id, officer.duty))
			{
				const ConsoleAddress owner{id, duty->console};
				if (station_stars(owner) < 3 || console_operator(owner) == 0)
					auto_assign_officer(owner, duty->id, false);
			}
	for (auto &o : state.officers)
	{
		if (o.condition == "stunned" && o.stun_until <= static_cast<std::int64_t>(ecs.get<Clock>().tick))
		{
			o.condition = "healthy";
			o.stun_until = 0;
			arrived = true;
			++state.revision;
		}
		if (o.condition == "stunned" && o.present)
			if (const auto *duty = personnel_duty(id, o.duty); duty && !duty->team.empty())
			{
				const auto at = personnel_destination(id, *duty);
				o.location = at.location;
				o.edge_to = at.edge_to;
				o.remaining = at.remaining;
			}
		if (!officer_mobile(o) || !o.escort.empty() || !o.facility.empty())
			continue;
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
			if (!physical_edge_open(id, o.location, o.edge_to))
				continue;
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
		    physical_route(id, d.compartments.size(), d.links, o.location,
		                   target.remaining > 0 ? static_cast<std::size_t>(target.edge_to) : target.location);
		if (route.first >= 0)
		{
			o.edge_to = route.first;
			o.remaining = route.second;
		}
	}
	(void)arrived;
}
// Read model of shared maintained native grants; never evaluates traits or pays costs.
std::vector<PropertyContribution> collect_personnel_properties(flecs::entity ship, const std::string &equipment,
                                                               const std::string &contribution) const
{
	std::vector<PropertyContribution> out;
	const auto effects = property_snapshot(ship);
	if (!effects || !ship.has<StableId>())
		return out;
	RuleSourceAvailabilityCache sources;
	for (const auto &entry : effects->contributions)
	{
		if (entry.equipment != equipment && (contribution.empty() || entry.equipment != contribution))
			continue;
		const auto *state = readonly_rule_execution(parent_rule_execution_key(entry.source));
		if (!state || state->owner_kind != RuleOwnerKind::Officer || !state->source_key.starts_with("trait:") ||
		    !rule_grant_available(ship.get<StableId>().id, entry.source, &sources))
			continue;
		auto row = entry;
		row.maintained = true;
		out.push_back(std::move(row));
	}
	return out;
}
std::pair<std::string, std::string> officer_speaker(EntityId id, const std::string &console,
                                                    const std::string &controller) const
{
	const auto &d = definition(id).personnel;
	const auto &state = entities.at(id).get<PersonnelState>();
	for (std::size_t i = 0; i < state.officers.size(); ++i)
		if (state.officers[i].present && officer_healthy(state.officers[i]))
		{
			const auto *duty = personnel_duty(id, state.officers[i].duty);
			if (duty && duty->controller == controller && duty->console == console)
				return {d.officers[i].name, d.officers[i].portrait.empty() ? "generic-crew" : d.officers[i].portrait};
		}
	return {controller, "generic-crew"};
}
bool auto_assign_officer(const ConsoleAddress &address, const std::string &duty_id, bool reassign)
{
	const auto *duty = personnel_duty(address.ship, duty_id);
	if (!duty || !personnel_owns(address, duty->department))
		return false;
	const auto &definition = this->definition(address.ship).personnel;
	const auto &state = entities.at(address.ship).get<PersonnelState>();
	for (const auto &officer : state.officers)
		if (officer.duty == duty_id && officer_mobile(officer))
			return true;
	std::optional<OperatingCommand> best;
	std::int64_t best_score = -1;
	for (std::size_t i = 0; i < definition.officers.size(); ++i)
	{
		const auto &officer = definition.officers[i];
		if (!reassign && !state.officers[i].duty.empty())
			continue;
		OperatingCommand command{address.ship, "personnel.assign", 0, officer.id};
		command.text = duty_id;
		command.generation = state.revision;
		if (!personnel_command(address, command, false, true))
			continue;
		std::int64_t score = state.officers[i].duty.empty() ? 1 : 0;
		for (const auto &trait : officer.traits)
			for (const auto &effect : definition.traits.at(trait).effects)
				for (const auto &c : effect.contributions)
					for (const auto &[system, roles] : duty->effects)
						if (std::find(roles.begin(), roles.end(), c.role) != roles.end())
							score += c.delta_permille;
		if (score > best_score)
		{
			best_score = score;
			best = command;
		}
	}
	return best && personnel_command(address, *best, true, true);
}
bool personnel_command(const ConsoleAddress &address, const OperatingCommand &command, bool execute, bool ai = false)
{
	if (!ai && station_stars(address) < 3)
		return false;
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
	if (state.officers[i].condition == "dead" ||
	    (state.officers[i].detained || (!officer_mobile(state.officers[i]) && command.action != "personnel.relieve")) ||
	    (!state.officers[i].facility.empty() && command.action != "personnel.relieve") ||
	    !state.officers[i].escort.empty() || away_reserved(address.ship, "officer:" + it->id))
		return false;
	const auto *duty = personnel_duty(address.ship, command.text);
	if (command.action == "personnel.relieve")
	{
		if (!command.text.empty())
			return false;
	}
	else if (command.action == "personnel.assign" || command.action == "personnel.swap")
	{
		if (!duty || !choir_assignment(address.ship, *it, duty) || duty->department != it->department ||
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
	if (command.action == "personnel.swap" && occupied < state.officers.size() &&
	    !officer_mobile(state.officers[occupied]))
		return false;
	if (occupied == i)
		return false;
	if (command.action == "personnel.swap" && (occupied == state.officers.size() || state.officers[i].duty.empty()))
		return false;
	if (command.action == "personnel.swap")
	{
		const auto *previous = personnel_duty(address.ship, state.officers[i].duty);
		if (!previous || !choir_assignment(address.ship, d.officers[occupied], previous) ||
		    (!previous->required_trait.empty() &&
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
		    physical_route(address.ship, d.compartments.size(), d.links, from, target.location).first < 0)
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
			stop_abilities_for_duty(address.ship, o.duty);
			o.present = false;
			o.duty = next;
		};
		const auto old = state.officers[i].duty;
		if (occupied < state.officers.size())
			detach(occupied, command.action == "personnel.swap" ? old : std::string{});
		detach(i, duty ? duty->id : std::string{});
		++state.revision;
	}
	return true;
}
PersonnelState personnel_snapshot(EntityId id) const
{
	auto snapshot = entities.at(id).get<PersonnelState>();
	for (auto &o : snapshot.officers)
		if (o.present && o.condition != "severe" && o.condition != "dead" && o.escort.empty())
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
	         {"manual", station_stars(address) >= 3},
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
			std::set<std::tuple<std::string, std::string, std::string>> shown;
			for (const auto &[system, roles] : duty.effects)
				for (const auto &role : roles)
					for (const auto &[trait_id, trait] : d.traits)
						for (const auto &effect : trait.effects)
							for (const auto &c : effect.contributions)
								if (c.role == role && shown.emplace(system, role, c.property).second)
									effects.push_back(
									    {{"system", system},
									     {"slot", role},
									     {"property", c.property},
									     // This readout is the requested relative factor; physical owners apply their own output caps.
									     {"multiplier",
									      apply_property_delta(
									          1000,
									          property_delta(entities.at(address.ship), system, c.property) +
									              property_fault_delta(entities.at(address.ship), system, c.property),
									          1000000001000LL)}});
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
				Json contributions = Json::array();
				for (const auto &effect : t.effects)
					for (const auto &c : effect.contributions)
						contributions.push_back({{"effect", effect.id},
						                         {"name", c.name},
						                         {"role", c.role},
						                         {"property", c.property},
						                         {"delta_permille", c.delta_permille}});
				traits.push_back({{"id", key}, {"label", t.label}, {"contributions", contributions}});
			}
			out["officers"].push_back({{"id", o.id},
			                           {"name", o.name},
			                           {"portrait", o.portrait},
			                           {"department", o.department},
			                           {"duty", r.duty},
			                           {"present", r.present},
			                           {"condition", r.detained ? "detained" : r.condition},
			                           {"stabilised", r.stabilised},
			                           {"stun_until", r.stun_until},
			                           {"facility", r.facility},
			                           {"treatment", r.treatment},
			                           {"location", away_officer_location(address.ship, o.id).empty()
			                                            ? d.compartments.at(r.location)
			                                            : away_officer_location(address.ship, o.id)},
			                           {"remaining", r.remaining},
			                           {"destination", r.edge_to < 0 ? std::string{} : d.compartments.at(r.edge_to)},
			                           {"traits", traits}});
			if (!o.abilities.empty())
			{
				Json abilities = Json::array();
				for (const auto &ability : o.abilities)
					abilities.push_back(
					    {{"id", ability},
					     {"label", definition(address.ship).abilities.catalog->definitions.at(ability).label}});
				out["officers"].back()["abilities"] = std::move(abilities);
			}
		}
	return out;
}
void restore_personnel(EntityId id, const Json &saved)
{
	auto state = saved.get<PersonnelState>();
	for (std::size_t i = 0; i < state.officers.size(); ++i)
	{
		const auto &row = saved.at("officers").at(i);
		const Json canonical = state.officers[i];
		for (const auto &[key, value] : row.items())
			if (!canonical.contains(key) || canonical.at(key) != value)
				throw std::runtime_error("Unexpected officer state fields");
		for (const auto *key :
		     {"location", "edge_to", "remaining", "stun_until", "treatment", "recovery", "casualty_tick"})
			if (row.contains(key) && !row.at(key).is_number_integer())
				throw std::runtime_error("Officer continuation requires integers");
	}
	const auto &d = definition(id).personnel;
	if (state.officers.size() != d.officers.size() || !state.revision || state.revision > 9007199254740991ULL)
		throw std::runtime_error("Invalid personnel save");
	std::set<std::string> duties;
	for (std::size_t i = 0; i < state.officers.size(); ++i)
	{
		const auto &o = state.officers[i];
		const auto *duty = personnel_duty(id, o.duty);
		if ((o.condition != "healthy" && o.condition != "stunned" && o.condition != "minor" &&
		     o.condition != "severe" && o.condition != "dead") ||
		    o.stun_until < 0 || o.treatment < 0 || o.treatment > 1728000000000LL || o.recovery < 0 ||
		    o.recovery > 1728000000000LL || o.casualty_tick < -1 || (o.stabilised && o.condition != "severe") ||
		    (o.condition != "stunned" && o.stun_until != 0) || (o.condition == "stunned" && o.stun_until == 0) ||
		    (o.condition == "dead" &&
		     (o.present || !o.duty.empty() || o.remaining || !o.facility.empty() || !o.escort.empty())) ||
		    (o.condition == "severe" && o.present))
			throw std::runtime_error("Invalid saved officer condition");
		if (!o.facility.empty())
		{
			const auto f = std::find_if(d.facilities.begin(), d.facilities.end(),
			                            [&](const auto &f)
			                            {
				                            return f.id == o.facility;
			                            });
			if (f == d.facilities.end() || o.condition != "severe" || !o.stabilised || o.remaining ||
			    d.compartments.at(o.location) != f->compartment || !o.escort.empty() ||
			    o.recovery >= f->recovery_ticks * 1000)
				throw std::runtime_error("Invalid saved facility patient");
		}
		if (!choir_assignment(id, d.officers[i], duty))
			throw std::runtime_error("Incompatible saved psychic assignment");
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
	for (const auto &f : d.facilities)
		if (std::count_if(state.officers.begin(), state.officers.end(),
		                  [&](const auto &o)
		                  {
			                  return o.facility == f.id;
		                  }) > f.capacity)
			throw std::runtime_error("Facility over capacity");
	entities.at(id).set<PersonnelState>(state);
}
