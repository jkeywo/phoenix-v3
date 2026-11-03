module;
#include "native_headers.hpp"

module phoenix.simulation;
import :crew_bindings;
import :away_adapters;

namespace phoenix::world_detail
{
auto Runtime::away_launch_blocker(std::size_t index) const -> std::string
{
	const auto &d = away_operations.definitions().at(index);
	const auto &m = away_operations.mission(index);
	const auto ship = away_ship(d);
	if (!ship || !entities.contains(d.target))
		return "Destination unavailable";
	if ((m.phase != AwayPhase::Open && m.phase != AwayPhase::Recovered) || !m.outcome.empty())
		return "Mission is not preparing";
	if (d.supply &&
	    (!supply_manifest(index) || m.supply_job || cargo_deliveries.size() >= 128 || material_requests.size() >= 4094))
		return "Supply manifest or delivery capacity is unavailable";
	for (const auto &slot : d.slots)
		if (slot.required && !m.assignments.contains(slot.id))
			return "Required mission slot is empty";
	if (!away_staged(ship, m))
		return "Waiting for the party and delivery method";
	if (!m.transport.empty())
	{
		const auto blocker = boarding_insertion_blocker(index, m.transport);
		if (!blocker.empty())
			return blocker;
	}
	const auto craft = away_transport_entity(ship, m.transport);
	if (craft && craft != ship && !craft_available(craft, d.id))
		return "Delivery craft is unavailable";
	if (craft && !away_blocker(index, false).empty())
		return away_blocker(index, false);
	for (const auto &slot : d.craft_slots)
		if (slot.role == "escort" && slot.required && !m.escorts.contains(slot.id))
			return "Required escort is missing";
	for (const auto &[slot, id] : m.escorts)
		if (!craft_available(id, d.id))
			return "Escort is unavailable";
	return {};
}

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

auto Runtime::away_command(const ConsoleAddress &a, const OperatingCommand &c, bool execute, bool ai, bool /*assisted*/)
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
	if (c.action == "away.care")
	{
		const auto slash = c.text.find('/');
		if (slash == std::string::npos || m.phase != AwayPhase::Deployed ||
		    c.value != static_cast<std::int64_t>(m.revision) || m.revision >= 9007199254740991ULL)
			return false;
		const auto team = c.text.substr(0, slash), compartment = c.text.substr(slash + 1);
		const auto equipment = away_equipment(a.ship, team);
		if (!equipment || equipment.get<Equipment>().kind != OperatingSystem::Team ||
		    !equipment.get<CombatMechanism>().definition.get<TeamDefinition>().capabilities.contains("medical") ||
		    std::none_of(m.assignments.begin(), m.assignments.end(),
		                 [&](const auto &assignment)
		                 {
			                 return assignment.second == "team:" + team;
		                 }))
			return false;
		bool permitted = false;
		for (const auto &controller : entities.at(a.ship).get<EntityControls>().controllers)
			if (controller.definition.console == a.console && controller.definition.id == c.controller &&
			    controller.generation == c.generation && controller_operation_owns(controller, "medical", team) &&
			    controller_operation_human(controller, "medical") != ai)
				permitted = true;
		const auto &personnel = definition(d.target).personnel;
		const auto to = std::find(personnel.compartments.begin(), personnel.compartments.end(), compartment);
		if (!permitted || to == personnel.compartments.end())
			return false;
		const auto target = static_cast<std::int64_t>(to - personnel.compartments.begin());
		const auto care = m.care.find(team);
		const auto from = care == m.care.end()  ? m.entry_compartment
		                  : care->second.travel ? care->second.edge_to
		                                        : care->second.location;
		if (from != target &&
		    physical_route(d.target, personnel.compartments.size(), personnel.links, from, target).first < 0)
			return false;
		if (execute)
		{
			auto &selected = m.care[team];
			if (care == m.care.end())
				selected.location = m.entry_compartment;
			selected.destination = target;
			selected.done = selected.work_done = false;
			selected.activity.clear();
			selected.continuation = {};
			++m.revision;
		}
		return true;
	}
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
			    ctrl.definition.operations.contains("contribution"))
				for (const auto &asset : ctrl.definition.operations.at("contribution"))
					if (const auto item = instance(entities.at(a.ship), asset);
					    item && (item->kind == OperatingSystem::Team || item->kind == OperatingSystem::ShuttleBay))
					{
						controlled_system = asset;
						break;
					}
	const ConsoleControllerRuntime *authority = nullptr;
	const bool officer_order = (c.action == "away.assign" && c.text.find("/officer:") != std::string::npos) ||
	                           (c.action == "away.withdraw" && m.assignments.contains(c.text) &&
	                            m.assignments.at(c.text).starts_with("officer:"));
	const auto operation = std::string(away_command_operation(c.action, officer_order));
	for (const auto &ctrl : entities.at(a.ship).get<EntityControls>().controllers)
		if (ctrl.definition.console == a.console && ctrl.definition.id == c.controller &&
		    ctrl.generation == c.generation)
		{
			if (controller_operation_owns(ctrl, operation, controlled_system) &&
			    controller_operation_human(ctrl, operation) != ai)
				authority = &ctrl;
		}
	if (!authority)
		return false;
	if (c.action == "away.select" || c.action == "away.unselect")
	{
		const auto team = instance(entities.at(a.ship), controlled_system);
		if (officer_order || !team || team->kind != OperatingSystem::Team)
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
		if (!d.supply || !away_dispatch(a, d.dispatch) ||
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
			++m.revision;
		}
		return true;
	}
	if (action == "send")
	{
		const auto supply = d.supply ? supply_manifest(index) : std::optional<CargoDelivery>{};
		if (d.supply && (!supply || m.supply_job || cargo_deliveries.size() >= 128 || material_requests.size() >= 4094))
			return false;
		if (!away_dispatch(a, d.dispatch) || !away_launch_blocker(index).empty())
			return false;
		const auto craft = away_transport_entity(a.ship, m.transport);
		const auto guidance = permitted_transport_contact(a, c.controller, d.target);
		if (craft && craft != a.ship && !guidance && entities.at(craft).get<CraftState>().target != d.target)
			return false;
		const auto rendezvous = permitted_transport_contact(a, c.controller, a.ship);
		if (!rendezvous)
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
				if (guidance)
				{
					s.target_position = {guidance->x, guidance->y};
					s.target_velocity = {guidance->vx, guidance->vy};
					s.target_altitude = guidance->altitude;
				}
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
				escort.target = craft ? craft : a.ship;
				escort.target_position = {rendezvous->x, rendezvous->y};
				escort.target_velocity = {rendezvous->vx, rendezvous->vy};
				escort.target_altitude = rendezvous->altitude;
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
			if ((!s.mission.empty() && s.mission != d.id) ||
			    (s.phase == CraftPhase::Lost || s.phase == CraftPhase::Consumed))
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
		if (selected.empty() || (definition(a.ship).craft_bays.contains(c.text) && selected == c.text))
			return false;
		if (selected == "ship" && !d.allow_ship_transport)
			return false;
		const auto craft = away_transport_entity(a.ship, selected);
		const bool upgrade = ai && !deployed && m.transport == "ship" && selected != "ship";
		if (upgrade && (!m.ai_docking_fallback || (craft && definition(craft).craft->role != "shuttle") ||
		                !away_blocker(index, false, &selected).empty()))
			return false;
		for (const auto &slot : d.craft_slots)
			if (slot.role == "transport" &&
			    (!craft || craft == a.ship ||
			     (!slot.craft_role.empty() && definition(craft).craft->role != slot.craft_role)))
				return false;
		if (craft && craft != a.ship && !craft_available(craft, d.id, deployed && m.transport_released) &&
		    !(deployed && selected == m.transport && entities.at(craft).get<CraftState>().mission == d.id))
			return false;
		auto guidance = craft && craft != a.ship ? permitted_transport_contact(a, c.controller, d.target)
		                                         : std::optional<BodyView>{};
		if (ai && craft && craft != a.ship && !guidance && authority->brain)
		{
			const auto &layout = *authority->brain->observation_layout;
			for (const auto &[objective, record] : ship_objectives(a.ship))
			{
				if (record.value.schema != layout.objective_schema || !active_objective(record))
					continue;
				const auto field = [&](ObjectiveField name) -> const KnowledgeValue *
				{
					const auto value = record.value.fields.find(layout.objective_keys[static_cast<std::size_t>(name)]);
					return value == record.value.fields.end() ? nullptr : &value->second;
				};
				const auto mission = field(ObjectiveField::mission), target = field(ObjectiveField::target);
				const auto x = field(ObjectiveField::destination_x), y = field(ObjectiveField::destination_y);
				if (mission && target && x && y && *mission == KnowledgeValue(d.id) &&
				    *target == KnowledgeValue(static_cast<std::int64_t>(d.target)) && std::get_if<std::int64_t>(x) &&
				    std::get_if<std::int64_t>(y))
				{
					guidance.emplace();
					guidance->id = d.target;
					guidance->x = std::get<std::int64_t>(*x);
					guidance->y = std::get<std::int64_t>(*y);
					break;
				}
			}
		}
		if (craft && craft != a.ship && !guidance && entities.at(craft).get<CraftState>().target != d.target)
			return false;
		if (execute)
		{
			if (guidance)
			{
				auto &state = entities.at(craft).get_mut<CraftState>();
				state.target = d.target;
				state.target_position = {guidance->x, guidance->y};
				state.target_velocity = {guidance->vx, guidance->vy};
				state.target_altitude = guidance->altitude;
			}
			if (deployed && selected != m.transport)
				release_away_transport(a.ship, m);
			m.transport = selected;
			m.ai_docking_fallback = ai && selected == "ship";
			if (deployed)
				m.transport_released = craft && craft != a.ship && entities.at(craft).get<CraftState>().mission != d.id;
			++m.revision;
		}
		return true;
	}
	if (action == "escort")
	{
		if ((m.phase != AwayPhase::Open && m.phase != AwayPhase::Recovered) || !m.outcome.empty())
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
			for (std::size_t i = 0; i < pd.officers.size(); ++i)
				if (ps.officers[i].duty == duty->id)
				{
					if (away_reserved(a.ship, "officer:" + pd.officers[i].id))
						return false;
					attached.push_back(pd.officers[i].id);
				}
		}
		if (ai && !m.transport.empty())
		{
			const auto physical = away_transport_entity(a.ship, m.transport);
			const auto capacity = physical ? definition(physical).transport.capacity
			                               : definition(a.ship).displacements.at(m.transport).capacity;
			const auto size = m.assignments.size() + m.accompanying.size() + 1 + attached.size();
			if (size > static_cast<std::size_t>(capacity))
				return false;
			if (!s->required && std::any_of(d.slots.begin(), d.slots.end(),
			                                [&](const auto &required)
			                                {
				                                return required.required && !m.assignments.contains(required.id);
			                                }))
				return false;
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
		}
		return true;
	}
	if (action == "withdraw")
	{
		if (!m.assignments.contains(c.text) || !away_asset_owned(a, m.assignments.at(c.text)))
			return false;
		if (execute)
		{
			withdraw_away_assignment(a.ship, m, c.text);
		}
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
