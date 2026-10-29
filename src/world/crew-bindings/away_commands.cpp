module;
#include "native_headers.hpp"

module phoenix.simulation;
import :crew_bindings;
import :away_adapters;

namespace phoenix::world_detail
{
auto Runtime::withdraw_away_assignment(EntityId ship, AwayMission &m, const std::string &slot) -> void
{
	const auto asset = m.assignments.at(slot);
	if (asset.starts_with("team:"))
	{
		const auto *duty = away_team_duty(ship, asset.substr(5));
		const auto &pd = definition(ship).personnel;
		const auto &ps = entities.at(ship).get<PersonnelState>();
		for (std::size_t i = 0; i < pd.officers.size(); ++i)
			if (ps.officers[i].duty == duty->id)
				std::erase(m.accompanying, pd.officers[i].id);
	}
	m.assignments.erase(slot);
	++m.revision;
}

auto Runtime::away_command(const ConsoleAddress &a, const OperatingCommand &c, bool execute, bool ai, bool assisted)
    -> bool
{
	if (!native_command_envelope(c))
		return false;
	if (!entities.at(a.ship).has<EntityControls>())
		return false;
	auto it = std::find_if(away_operations.definitions().begin(), away_operations.definitions().end(),
	                       [&](const auto &d)
	                       {
		                       return d.id == c.system && away_ship(d) == a.ship;
	                       });
	if (it == away_operations.definitions().end())
		return false;
	const auto index = it - away_operations.definitions().begin();
	const auto &d = *it;
	auto &m = away_operations.missions()[index];
	std::string controlled_system;
	if (c.action == "away.transport" || c.action == "away.escort")
	{
		auto selection = c.action == "away.escort" ? c.text.substr(c.text.find('/') + 1)
		                 : c.text.empty()          ? m.transport
		                                           : c.text;
		if (selection.empty() && c.action == "away.escort")
		{
			const auto slot = c.text.substr(0, c.text.find('/'));
			if (m.escorts.contains(slot))
				selection = "craft:" + std::to_string(m.escorts.at(slot));
		}
		const auto physical = away_transport_entity(a.ship, selection);
		if (physical == a.ship)
		{
			for (auto e : entities.at(a.ship).get<EquipmentIndex>().values)
				if (e.get<Equipment>().kind == OperatingSystem::Dispatch &&
				    enabled_system_owner(a.ship, e.get<Equipment>().id) == a)
					controlled_system = e.get<Equipment>().id;
		}
		else
			controlled_system = physical ? entities.at(physical).get<CraftState>().bay : selection;
	}
	else if (c.action == "away.contribute")
	{
		if (c.text != "1" && c.text != "0")
			return false;
		for (const auto &ctrl : entities.at(a.ship).get<EntityControls>().controllers)
			if (ctrl.definition.id == c.controller && ctrl.definition.console == a.console)
				for (const auto &ch : ctrl.definition.commands)
					for (auto consumer : ch.consumers)
						if (const auto &system = definition(a.ship).fitted->values.at(consumer);
						    system.kind == OperatingSystem::Team || system.kind == OperatingSystem::ShuttleBay)
							controlled_system = system.id;
	}
	else if (c.action == "away.assign" || c.action == "away.select")
	{
		const auto pos = c.text.find("/team:");
		if (pos != std::string::npos)
			controlled_system = c.text.substr(pos + 6);
	}
	else if (c.action == "away.withdraw" || c.action == "away.unselect")
	{
		for (std::size_t i = 0; i < away_operations.definitions().size(); ++i)
			if (away_operations.definitions()[i].id == c.system &&
			    away_ship(away_operations.definitions()[i]) == a.ship)
			{
				const auto &m = away_operations.missions()[i];
				if (m.assignments.contains(c.text) && m.assignments.at(c.text).starts_with("team:"))
					controlled_system = m.assignments.at(c.text).substr(5);
			}
	}
	else
	{
		if (!c.text.empty() && c.action != "away.quantity")
			return false;
		for (auto e : entities.at(a.ship).get<EquipmentIndex>().values)
			if (e.get<Equipment>().kind == OperatingSystem::Dispatch &&
			    enabled_system_owner(a.ship, e.get<Equipment>().id) == a &&
			    e.get<Equipment>().id == away_dispatch_system(a.ship, d.dispatch))
				controlled_system = e.get<Equipment>().id;
		if (controlled_system.empty())
			return false;
	}
	if (c.action == "away.contribute")
		for (const auto &ctrl : entities.at(a.ship).get<EntityControls>().controllers)
			if (ctrl.definition.id == c.controller && ctrl.definition.console == a.console &&
			    !ctrl.definition.widget.empty() && ctrl.definition.operations.contains("contribution"))
				for (const auto &asset : ctrl.definition.operations.at("contribution"))
					if (const auto item = instance(entities.at(a.ship), asset);
					    item && (item->kind == OperatingSystem::Team || item->kind == OperatingSystem::ShuttleBay))
					{
						controlled_system = asset;
						break;
					}
	bool authority = false;
	const bool officer_order = (c.action == "away.assign" && c.text.find("/officer:") != std::string::npos) ||
	                           (c.action == "away.withdraw" && m.assignments.contains(c.text) &&
	                            m.assignments.at(c.text).starts_with("officer:"));
	const auto operation = std::string(away_command_operation(c.action, officer_order));
	bool widget_command = false;
	for (const auto &ctrl : entities.at(a.ship).get<EntityControls>().controllers)
		if (ctrl.definition.console == a.console && ctrl.definition.id == c.controller &&
		    ctrl.generation == c.generation && !ctrl.definition.widget.empty())
		{
			widget_command = true;
			authority |= controller_operation_owns(ctrl, operation, controlled_system) &&
			             controller_operation_human(ctrl, operation) != ai;
		}
	for (const auto &ctrl : entities.at(a.ship).get<EntityControls>().controllers)
		if (ctrl.definition.console == a.console && ctrl.definition.id == c.controller &&
		    (ctrl.definition.widget.empty() &&
		     (ctrl.human != ai || (!ai && c.action == "away.contribute" && !controlled_system.empty()) ||
		      (!ai && (c.action == "away.transport" || c.action == "away.escort") &&
		       assisted_workload(a, "transport")) ||
		      (!ai && station_stars(a) < 3 && !controlled_system.empty() &&
		       (c.action == "away.assign" || c.action == "away.withdraw")))) &&
		    ctrl.generation == c.generation)
		{
			if (controlled_system.empty())
				authority = true;
			for (const auto &command : ctrl.definition.commands)
				for (auto consumer : command.consumers)
					if (definition(a.ship).fitted->values.at(consumer).id == controlled_system)
						authority = true;
		}
	if (!authority)
		return false;
	if (c.action == "away.select" || c.action == "away.unselect")
	{
		const auto team = instance(entities.at(a.ship), controlled_system);
		if (!widget_command || officer_order || !team || team->kind != OperatingSystem::Team)
			return false;
	}
	if (m.phase == AwayPhase::Hidden || m.revision >= 9007199254740991ULL ||
	    away_operations.sequence() >= 9007199254740991ULL || c.value != static_cast<std::int64_t>(m.revision))
		return false;
	auto action = c.action.substr(5);
	if (action == "select")
		action = "assign";
	if (action == "unselect")
		action = "withdraw";
	if (action == "quantity")
	{
		if (!d.supply || (!widget_command && station_stars(a) < 3) || !away_dispatch(a, d.dispatch) ||
		    (m.phase != AwayPhase::Open && m.phase != AwayPhase::Recovered) || m.supply_job)
			return false;
		const auto quantity = parse_material_number(c.text);
		if (quantity < 0 || quantity > d.supply->limit)
			return false;
		const auto manifest = supply_manifest(index, quantity);
		if (quantity && (!manifest || manifest->amount != quantity))
			return false;
		if (execute)
		{
			m.supply_quantity = quantity;
			++m.revision;
		}
		return true;
	}
	if (action == "contribute")
	{
		if (controlled_system.empty() || (m.phase != AwayPhase::Open && m.phase != AwayPhase::Recovered))
			return false;
		if (execute)
		{
			if (c.text == "1")
				m.authorised_departments.insert(a.console);
			else
			{
				m.authorised_departments.erase(a.console);
				if (!m.transport.empty() && away_asset_owned(a, "transport:" + m.transport))
					m.transport.clear();
				std::erase_if(m.escorts,
				              [&](const auto &s)
				              {
					              return away_asset_owned(a, "craft:" + std::to_string(s.second));
				              });
				std::vector<std::string> slots;
				for (const auto &[slot, asset] : m.assignments)
					if (away_asset_owned(a, asset))
						slots.push_back(slot);
				for (const auto &slot : slots)
					withdraw_away_assignment(a.ship, m, slot);
			}
			++m.revision;
			if (c.text == "1")
				prepare_away_contribution(a, index);
		}
		return true;
	}
	if (action == "open")
	{
		if (!away_dispatch(a, d.dispatch) || m.phase != AwayPhase::Available)
			return false;
		if (execute)
		{
			m.phase = AwayPhase::Open;
			m.authorised_departments.insert(a.console);
			if (d.allow_ship_transport &&
			    (widget_command ? !manual_workload(a, "transport", away_dispatch_system(a.ship, d.dispatch))
			                    : station_stars(a) < 3))
				m.transport = "ship";
			++m.revision;
			prepare_away_officers(a, index);
			prepare_away_contribution(a, index);
		}
		return true;
	}
	if (action == "send")
	{
		const auto supply = d.supply ? supply_manifest(index) : std::optional<CargoDelivery>{};
		if (d.supply && (!supply || m.supply_job || cargo_deliveries.size() >= 128 || material_requests.size() >= 4094))
			return false;
		if (!away_dispatch(a, d.dispatch) || (m.phase != AwayPhase::Open && m.phase != AwayPhase::Recovered) ||
		    !m.outcome.empty() || !away_staged(a.ship, m))
			return false;
		if (!m.transport.empty() && !boarding_insertion_blocker(index, m.transport).empty())
			return false;
		if (away_transport_entity(a.ship, m.transport) == a.ship && !away_blocker(index, false).empty())
			return false;
		const auto craft = away_transport_entity(a.ship, m.transport);
		if (craft && craft != a.ship && (!craft_available(craft, d.id) || !away_blocker(index, false).empty()))
			return false;
		for (const auto &s : d.slots)
			if (s.required && !m.assignments.contains(s.id))
				return false;
		for (const auto &slot : d.craft_slots)
			if (slot.role == "escort" && slot.required && !m.escorts.contains(slot.id))
				return false;
		for (const auto &[slot, id] : m.escorts)
			if (!craft_available(id, d.id))
				return false;
		if (execute)
		{
			m.phase = AwayPhase::Outbound;
			m.sequence = away_operations.next_sequence();
			m.transfer = 0;
			if (supply)
			{
				auto job = *supply;
				job.id = next_cargo_delivery++;
				m.supply_job = job.id;
				m.supply_quantity = job.amount;
				cargo_deliveries.push_back(job);
				sync_cargo_stock(cargo_deliveries.back());
				std::erase_if(material_requests,
				              [&](const auto &r)
				              {
					              return r.automatic && r.destination == a.ship && r.to == job.from &&
					                     r.material == job.material;
				              });
				supply_transfer(cargo_deliveries.back(), a.ship, job.from, job.carrier, job.cargo, job.payment,
				                job.payment_amount);
			}
			if (craft && craft != a.ship)
			{
				auto e = entities.at(craft);
				auto &s = e.get_mut<CraftState>();
				s.mission = d.id;
				s.target = d.target;
				s.target_position = entities.at(d.target).get<Pose>().position;
				s.target_velocity = entities.at(d.target).get<Motion>().velocity;
				s.target_altitude = altitude(d.target);
				s.order = "transport";
				m.aboard_craft = true;
				m.transport_released = false;
				if (m.escorts.empty() && !d.supply)
					launch_craft(e);
			}
			for (const auto &[slot, id] : m.escorts)
			{
				auto e = entities.at(id);
				auto &escort = e.get_mut<CraftState>();
				escort.mission = d.id;
				escort.order = "escort";
				escort.target = craft;
				escort.target_position = entities.at(craft).get<Pose>().position;
				escort.target_velocity = entities.at(craft).get<Motion>().velocity;
				escort.target_altitude = altitude(craft);
				launch_craft(e);
			}
			++m.revision;
		}
		return true;
	}
	if (action == "recall")
	{
		if (d.supply && away_dispatch(a, d.dispatch) && m.supply_job &&
		    (m.phase == AwayPhase::Outbound || m.phase == AwayPhase::Inbound))
		{
			if (execute)
				for (auto &job : cargo_deliveries)
					if (job.id == m.supply_job)
					{
						request_cargo_return(job);
						++m.revision;
					}
			return true;
		}
		if (!away_dispatch(a, d.dispatch) || m.phase != AwayPhase::Deployed || m.boarding.prize_crew ||
		    !away_blocker(index, false).empty())
			return false;
		const auto craft = away_transport_entity(a.ship, m.transport);
		if (craft && craft != a.ship)
		{
			const auto &s = entities.at(craft).get<CraftState>();
			if ((!s.mission.empty() && s.mission != d.id) || s.phase == CraftPhase::Lost)
				return false;
		}
		if (execute)
		{
			m.phase = AwayPhase::Inbound;
			return_away_escorts(m);
			m.sequence = away_operations.next_sequence();
			m.transfer = 0;
			++m.revision;
			if (craft && craft != a.ship)
			{
				// This is the party's recovery order. Independent craft orders retain radio delivery.
				request_craft_pickup(craft);
			}
		}
		return true;
	}
	if (action == "cancel")
	{
		if (away_dispatch(a, d.dispatch) && m.phase == AwayPhase::Open && !m.supply_job)
		{
			if (execute)
			{
				m.assignments.clear();
				m.accompanying.clear();
				m.care.clear();
				m.escorts.clear();
				m.transport.clear();
				m.authorised_departments.clear();
				m.phase = AwayPhase::Available;
				++m.revision;
			}
			return true;
		}
		if (!away_dispatch(a, d.dispatch) || (m.phase != AwayPhase::Outbound && m.phase != AwayPhase::Inbound))
			return false;
		const auto selected = away_transport_entity(a.ship, m.transport);
		if (selected && selected != a.ship && torpedo_launched(entities.at(selected)))
			return false;
		if (execute)
		{
			return_away_escorts(m);
			if (d.supply)
			{
				for (auto &job : cargo_deliveries)
					if (job.id == m.supply_job)
						request_cargo_return(job);
				++m.revision;
				return true;
			}
			const auto craft = away_transport_entity(a.ship, m.transport);
			if (craft && craft != a.ship)
			{
				request_craft_pickup(craft);
				return true;
			}
			m.phase = m.deployed() ? AwayPhase::Deployed : AwayPhase::Open;
			m.transfer = 0;
			++m.revision;
		}
		return true;
	}
	if (action == "transport")
	{
		if (!widget_command && !ai && !assisted && !manual_workload(a, "transport"))
			return false;
		const bool deployed = m.phase == AwayPhase::Deployed;
		if (c.text.empty())
		{
			if ((m.phase != AwayPhase::Open && m.phase != AwayPhase::Recovered) || !m.outcome.empty() ||
			    m.transport.empty() || !away_asset_owned(a, "transport:" + m.transport))
				return false;
			if (execute)
			{
				m.transport.clear();
				++m.revision;
			}
			return true;
		}
		if ((!deployed && ((m.phase != AwayPhase::Open && m.phase != AwayPhase::Recovered) || !m.outcome.empty())) ||
		    m.aboard_craft || !away_asset_owned(a, "transport:" + c.text))
			return false;
		const auto selected = canonical_away_transport(a.ship, c.text, d.id);
		if (definition(a.ship).craft_bays.contains(c.text) && selected == c.text)
			return false;
		if (selected == "ship" && !d.allow_ship_transport)
			return false;
		const auto craft = away_transport_entity(a.ship, selected);
		for (const auto &slot : d.craft_slots)
			if (slot.role == "transport" &&
			    (!craft || craft == a.ship ||
			     (!slot.craft_role.empty() && definition(craft).craft->role != slot.craft_role)))
				return false;
		if (craft && craft != a.ship && !craft_available(craft, d.id) &&
		    !(deployed && selected == m.transport && entities.at(craft).get<CraftState>().mission == d.id))
			return false;
		if (execute)
		{
			if (deployed && selected != m.transport)
				release_away_transport(a.ship, m);
			m.transport = selected;
			if (deployed)
				m.transport_released = craft && craft != a.ship && entities.at(craft).get<CraftState>().mission != d.id;
			++m.revision;
		}
		return true;
	}
	if (action == "escort")
	{
		if ((!widget_command && !ai && !assisted && !manual_workload(a, "transport")) ||
		    (m.phase != AwayPhase::Open && m.phase != AwayPhase::Recovered) || !m.outcome.empty())
			return false;
		const auto slash = c.text.find('/');
		if (slash == std::string::npos)
			return false;
		const auto slot = c.text.substr(0, slash), asset = c.text.substr(slash + 1);
		if (asset.empty())
		{
			if (!m.escorts.contains(slot) || !away_asset_owned(a, "craft:" + std::to_string(m.escorts.at(slot))))
				return false;
			if (execute)
			{
				m.escorts.erase(slot);
				++m.revision;
			}
			return true;
		}
		const auto id = away_transport_entity(a.ship, asset);
		if (!id || id == a.ship || !away_asset_owned(a, asset) || !craft_available(id, d.id) || m.transport == asset ||
		    std::any_of(m.escorts.begin(), m.escorts.end(),
		                [&](const auto &s)
		                {
			                return s.first != slot && s.second == id;
		                }))
			return false;
		const auto authored = std::find_if(d.craft_slots.begin(), d.craft_slots.end(),
		                                   [&](const auto &s)
		                                   {
			                                   return s.id == slot && s.role == "escort";
		                                   });
		if (authored == d.craft_slots.end() || definition(id).craft->role != authored->craft_role)
			return false;
		if (execute)
		{
			m.escorts[slot] = id;
			m.authorised_departments.insert(a.console);
			++m.revision;
			prepare_away_officers(a, index);
		}
		return true;
	}
	if ((m.phase != AwayPhase::Open && m.phase != AwayPhase::Recovered) || !m.outcome.empty())
		return false;
	if (action == "assign")
	{
		const auto separator = c.text.find('/');
		if (separator == std::string::npos)
			return false;
		const auto slot = c.text.substr(0, separator), asset = c.text.substr(separator + 1);
		if (!widget_command && !ai && !assisted && station_stars(a) < 3 && asset.starts_with("officer:"))
			return false;
		const auto s = std::find_if(d.slots.begin(), d.slots.end(),
		                            [&](const auto &s)
		                            {
			                            return s.id == slot;
		                            });
		if (s == d.slots.end() || m.assignments.contains(slot) || !away_asset_owned(a, asset) ||
		    away_reserved(a.ship, asset) || !away_asset_healthy(a.ship, asset) || !away_slot_accepts(a.ship, *s, asset))
			return false;
		std::vector<std::string> attached;
		const auto &pd = definition(a.ship).personnel;
		auto &ps = entities.at(a.ship).get_mut<PersonnelState>();
		if (asset.starts_with("team:"))
		{
			const auto *duty = away_team_duty(a.ship, asset.substr(5));
			if (execute && (widget_command ? !manual_workload(a, "mission_staffing") : station_stars(a) < 3))
				auto_assign_officer(a, duty->id, true);
			for (std::size_t i = 0; i < pd.officers.size(); ++i)
				if (ps.officers[i].duty == duty->id)
				{
					if (away_reserved(a.ship, "officer:" + pd.officers[i].id))
						return false;
					attached.push_back(pd.officers[i].id);
				}
		}
		if (execute)
		{
			m.assignments.emplace(slot, asset);
			m.accompanying.insert(m.accompanying.end(), attached.begin(), attached.end());
			if (asset.starts_with("officer:"))
				for (std::size_t i = 0; i < pd.officers.size(); ++i)
					if (pd.officers[i].id == asset.substr(8))
					{
						ps.officers[i].duty.clear();
						ps.officers[i].present = false;
					}
			++ps.revision;
			++m.revision;
			if (!assisted)
				prepare_away_officers(a, index);
		}
		return true;
	}
	if (action == "withdraw")
	{
		if (!m.assignments.contains(c.text) || !away_asset_owned(a, m.assignments.at(c.text)))
			return false;
		if (!widget_command && !ai && !assisted && station_stars(a) < 3 &&
		    m.assignments.at(c.text).starts_with("officer:"))
			return false;
		if (execute)
		{
			withdraw_away_assignment(a.ship, m, c.text);
		}
		return true;
	}
	return false;
}

auto Runtime::prepare_away_tick_contributions(std::size_t i) -> void
{
	auto &m = away_operations.missions()[i];
	const auto &d = away_operations.definitions()[i];
	const auto ship = away_ship(d);
	if (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered)
		for (const auto &console : definition(ship).consoles)
		{
			const ConsoleAddress owner{ship, console.id};
			if (!m.authorised_departments.contains(console.id))
				for (const auto &controller : entities.at(ship).get<EntityControls>().controllers)
					if (controller.definition.console == console.id &&
					    (controller.definition.widget.empty()
					         ? !controller.human
					         : controller_operation_owns(controller, "contribution", {}) &&
					               !controller_operation_human(controller, "contribution")))
					{
						bool team_controller = false;
						for (const auto &command : controller.definition.commands)
							for (const auto consumer : command.consumers)
								team_controller |=
								    definition(ship).fitted->values.at(consumer).kind == OperatingSystem::Team;
						if (controller.definition.widget.empty() && console_operator(owner) && !team_controller)
							continue;
						OperatingCommand c{ship, "away.contribute", static_cast<std::int64_t>(m.revision), d.id};
						c.controller = controller.definition.id;
						c.generation = controller.generation;
						c.text = "1";
						if (away_command(owner, c, true, true))
							break;
					}
			prepare_away_contribution(owner, i);
		}
	if (m.phase == AwayPhase::Open || m.phase == AwayPhase::Recovered)
		for (const auto &slot : d.slots)
			if (slot.assign_ai && slot.kind == "team" && !m.assignments.contains(slot.id) && m.outcome.empty())
				for (const auto &duty : definition(ship).personnel.duties)
				{
					if (duty.team.empty())
						continue;
					const auto asset = "team:" + duty.team;
					if (away_reserved(ship, asset) || !away_asset_healthy(ship, asset) ||
					    !away_slot_accepts(ship, slot, asset))
						continue;
					const auto owner = enabled_system_owner(ship, duty.team);
					for (const auto &controller : entities.at(ship).get<EntityControls>().controllers)
						if (controller.definition.console == owner.console &&
						    !controller_operation_human(controller, "transport") && !m.assignments.contains(slot.id))
						{
							OperatingCommand command{ship, "away.assign", static_cast<std::int64_t>(m.revision), d.id};
							command.text = slot.id + "/" + asset;
							command.controller = controller.definition.id;
							command.generation = controller.generation;
							away_command(owner, command, true, true);
						}
				}
}

auto Runtime::prepare_away_assisted_send(std::size_t i) -> bool
{
	auto &m = away_operations.missions()[i];
	const auto &d = away_operations.definitions()[i];
	const auto ship = away_ship(d);
	const auto owner = enabled_system_owner(ship, away_dispatch_system(ship, d.dispatch));
	const auto *native = native_operation_controller(owner, "mission_send", away_dispatch_system(ship, d.dispatch));
	if (native ? !controller_operation_human(*native, "mission_send") : station_stars(owner) == 1)
		for (const auto &controller : entities.at(ship).get<EntityControls>().controllers)
			if (controller.definition.console == owner.console)
			{
				OperatingCommand command{ship, "away.send", static_cast<std::int64_t>(m.revision), d.id};
				command.controller = controller.definition.id;
				command.generation = controller.generation;
				if (away_command(owner, command, true, controller.definition.widget.empty() ? !controller.human : true,
				                 true))
					return true;
			}
	return false;
}

auto Runtime::stage_away(EntityId ship, AwayMission &m) -> void
{
	NativeAwayBinding binding(*this);
	away_operations.stage(ship, m, binding.execution);
}

auto Runtime::advance_away() -> void
{
	NativeAwayBinding binding(*this);
	away_operations.advance(ecs.get<Clock>().tick, binding.execution);
}

auto Runtime::away_asset_label(EntityId ship, const std::string &asset) const -> std::string
{
	if (asset.starts_with("officer:"))
		for (const auto &o : definition(ship).personnel.officers)
			if (o.id == asset.substr(8))
				return o.name;
	if (asset.starts_with("team:"))
	{
		const auto *d = away_team_duty(ship, asset.substr(5));
		if (d)
			return d->label;
	}
	return asset;
}
} // namespace phoenix::world_detail
