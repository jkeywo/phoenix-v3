module;
#include "native_headers.hpp"

module phoenix.simulation;
import :scenario_bindings;

namespace phoenix::world_detail
{
auto Runtime::interdiction_docking_permission(EntityId ship, EntityId target) const -> bool
{
	return interdiction_book.docking_permission(ship, target);
}

auto Runtime::restore_interdiction_ships(const Json &rows, const Json &bodies) -> void
{
	std::map<std::string, std::string> fittings;
	std::set<EntityId> existing;
	for (const auto &[id, entity] : entities)
		existing.insert(id);
	for (std::size_t i = 0; i < interdiction_book.size(); ++i)
	{
		const auto &d = interdiction_book.definition(i);
		fittings.emplace(d.slot, entities.at(slot_entities.at(d.slot)).get<Definition>().name);
	}
	for (const auto &ship : interdiction_book.restore_ships(rows, bodies, fittings, existing))
		create(ship.id, ship.definition, {{"x_mm", 0}, {"y_mm", 0}, {"heading_mdeg", 0}});
}

auto Runtime::record_interdiction_attack(EntityId source, EntityId target) -> void
{
	if (entities.contains(source) && entities.at(source).has<CraftState>())
		source = entities.at(source).get<CraftState>().home;
	for (std::size_t i = 0; i < interdiction_book.size(); ++i)
	{
		const auto player = slot_entities.at(interdiction_book.definition(i).slot);
		interdiction_book.record_attack(i, operational_owner(source), operational_owner(player), target);
	}
}

auto Runtime::record_interdiction_insertion(EntityId ship, EntityId target) -> void
{
	for (std::size_t i = 0; i < interdiction_book.size(); ++i)
		interdiction_book.record_insertion(i, ship, slot_entities.at(interdiction_book.definition(i).slot), target);
}

auto Runtime::encounter_owns_controller(EntityId ship, const ConsoleControllerSpec &controller) const -> bool
{
	bool owned = false;
	for (std::size_t i = 0; i < interdiction_book.size(); ++i)
	{
		owned |= interdiction_book.participant(i, ship);
		if (ship == interdiction_book.definition(i).target && interdiction_book.state(i).captured &&
		    captured_by.contains(ship))
			owned |= interdiction_book.participant(i, operational_owner(ship));
	}
	return owned && std::any_of(controller.commands.begin(), controller.commands.end(),
	                            [](const auto &command)
	                            {
		                            return encounter_manages_action(command.action);
	                            });
}

auto Runtime::release_encounter_navigation(EntityId ship) -> void
{
	encounter_command(ship, OperatingAction::Destination, 0);
	encounter_command(ship, OperatingAction::FollowContact, 0);
	encounter_command(ship, OperatingAction::MatchSpeed, 0);
}

auto Runtime::encounter_command(EntityId ship, OperatingAction action, std::int64_t value) -> bool
{
	if (!entities.contains(ship) || !entities.at(ship).has<EntityControls>())
		return false;
	auto &controllers = entities.at(ship).get_mut<EntityControls>().controllers;
	bool accepted = false;
	for (std::size_t i = 0; i < controllers.size(); ++i)
	{
		auto &c = controllers[i];
		if (c.human)
			continue;
		for (std::size_t j = 0; j < c.definition.commands.size(); ++j)
			if (c.definition.commands[j].action == action)
			{
				if (action == OperatingAction::WeaponTarget && value)
				{
					const auto contacts = capture_sensor_contacts({ship, c.definition.console});
					if (std::none_of(contacts.begin(), contacts.end(),
					                 [&](const auto &contact)
					                 {
						                 return contact.id == static_cast<EntityId>(value);
					                 }))
						continue;
				}
				accepted |= commit_controller(
				    {{ship, c.definition.console, c.definition.id},
				     i,
				     j,
				     std::clamp(value, c.definition.commands[j].minimum, c.definition.commands[j].maximum),
				     c.generation,
				     0,
				     false,
				     c.definition.commands[j].initial_arguments});
			}
	}
	return accepted;
}

auto Runtime::encounter_contact(EntityId ship, EntityId target) const -> std::optional<fixed::Vec>
{
	for (const auto &c : entities.at(ship).get<EntityControls>().controllers)
		if (!c.human)
			for (const auto &channel : c.definition.commands)
				if (channel.action == OperatingAction::Speed || channel.action == OperatingAction::Heading)
				{
					const auto contacts = capture_sensor_contacts({ship, c.definition.console});
					for (const auto &contact : contacts)
						if (contact.id == target)
							return fixed::Vec{contact.x, contact.y};
				}
	return {};
}

auto Runtime::encounter_move(EntityId ship, fixed::Vec destination, std::int64_t stop_radius, std::int64_t cruise_speed)
    -> void
{
	if (entities.at(ship).has<DockConnection>())
		return;
	const auto demand =
	    encounter_motion_demand(entities.at(ship).get<Pose>(), {destination, stop_radius, cruise_speed});
	encounter_command(ship, OperatingAction::Heading, demand.heading);
	encounter_command(ship, OperatingAction::Speed, demand.speed);
}

auto Runtime::encounter_dock_order(EntityId ship, const std::string &action, EntityId target) -> bool
{
	if (action == "dock.undock" && entities.at(ship).has<DockConnection>() &&
	    entities.at(ship).get<DockConnection>().phase == "separating")
		return true;
	if (definition(ship).docking.empty())
		return false;
	const auto &port = definition(ship).docking.front();
	for (const auto &c : entities.at(ship).get<EntityControls>().controllers)
		if (!c.human && c.definition.id == port.controller)
		{
			OperatingCommand command{
			    ship, action, static_cast<std::int64_t>(entities.at(ship).get<DockingState>().revision), port.id};
			command.controller = c.definition.id;
			command.generation = c.generation;
			if (target)
			{
				if (definition(target).docking.empty())
					return false;
				command.text = std::to_string(target) + "/" + definition(target).docking.front().id;
			}
			return docking_command({ship, c.definition.console}, command, true, true);
		}
	return false;
}

auto Runtime::apply_occupation(EntityId target, const std::vector<std::string> &occupation, bool lethal) -> void
{
	auto e = entities.at(target);
	for (auto child : e.get<EquipmentIndex>().values)
		if (auto *m = child.try_get_mut<CombatMechanism>(); m && m->definition.kind == OperatingSystem::Team)
		{
			if (std::find(occupation.begin(), occupation.end(), child.get<Equipment>().id) == occupation.end())
			{
				if (lethal)
					m->get<TeamState>().condition = "lost";
			}
			else
			{
				m->get<TeamState>().condition = "healthy";
				m->get<TeamState>().detained = false;
				child.get_mut<Equipment>().enabled = true;
			}
		}
	if (lethal && e.has<PersonnelState>())
	{
		auto &p = e.get_mut<PersonnelState>();
		for (auto &o : p.officers)
		{
			o.condition = "dead";
			o.present = false;
			o.duty.clear();
			o.detained = false;
			o.casualty_tick = ecs.get<Clock>().tick;
			o.stun_until = o.treatment = o.recovery = o.remaining = 0;
			o.stabilised = false;
			o.edge_to = -1;
			o.facility.clear();
			o.escort.clear();
		}
		++p.revision;
	}
}

auto Runtime::restore_repairable_ship(EntityId target) -> void
{
	auto &s = entities.at(target).get_mut<CombatStructure>();
	for (std::size_t i = 0; i < s.compartments.size(); ++i)
	{
		auto &c = s.compartments[i];
		apply_structural_repair(s, i, (s.definition.compartments[i].maximum - c.hp) * rate * 1000);
	}
	for (auto &[system, faults] : s.faults)
		for (std::size_t i = 0; i < faults.size(); ++i)
			finish_fault_repair(s, system, i);
}

auto Runtime::advance_interdictions() -> void
{
	struct Native final : InterdictionNative
	{
		Impl &world;
		explicit Native(Impl &owner) : world(owner)
		{
		}
		InterdictionTickFacts observe(const InterdictionDefinition &d, const InterdictionState &s) const override
		{
			InterdictionTickFacts facts;
			facts.player = world.slot_entities.at(d.slot);
			facts.player_definition = world.entities.at(facts.player).get<Definition>().name;
			for (auto id : s.ships)
				if (!world.truly_destroyed(id) && !world.departed(id))
					facts.live_ships.push_back(id);
			facts.target_lost = world.truly_destroyed(d.target) || world.departed(d.target);
			facts.claimed = world.captured_by.contains(d.target) &&
			                world.operational_owner(d.target) == world.operational_owner(facts.player);
			facts.player_active = !world.truly_destroyed(facts.player) && !world.departed(facts.player);
			facts.player_position = world.entities.at(facts.player).get<Pose>().position;
			facts.target_position = world.entities.at(d.target).get<Pose>().position;
			if (s.boarder)
			{
				facts.boarder_alive = !world.truly_destroyed(s.boarder);
				facts.boarder_position = world.entities.at(s.boarder).get<Pose>().position;
				facts.boarder_docked = world.entities.at(s.boarder).has<DockConnection>();
				if (facts.boarder_alive)
					facts.dock_ready = world.dock_ready(s.boarder, d.target);
			}
			facts.start =
			    d.start_event.empty() || (world.scenario && !world.scenario->runtime().events(d.start_event).empty());
			if (s.conversation && world.conversations.conversations.contains(s.conversation))
				facts.answer = world.conversations.conversations.at(s.conversation).prompt.answer;
			return facts;
		}
		EntityId spawn_ship(const InterdictionShip &spawn) override
		{
			const auto id = world.next_entity++;
			world.create(id, spawn.definition, {{"x_mm", spawn.x}, {"y_mm", spawn.y}, {"heading_mdeg", spawn.heading}});
			world.emit_arrival_audio(id);
			return id;
		}
		std::uint64_t open_dialogue(const InterdictionDialogue &request) override
		{
			return world.conversations.open(request.program, request.roles, request.participants);
		}
		std::optional<std::uint64_t> report_dialogue(std::uint64_t conversation, const std::string &line) override
		{
			if (!world.conversations.report(conversation, line))
				return {};
			return world.conversations.conversations.at(conversation).messages.back().id;
		}
		void offer_dialogue(std::uint64_t conversation, const std::string &line) override
		{
			world.conversations.offer(conversation, line);
		}
		void clear_prompt(std::uint64_t conversation) override
		{
			world.conversations.conversations.at(conversation).prompt = {};
		}
		void expire_pleas(std::uint64_t conversation, std::span<const std::uint64_t> receipts) override
		{
			if (conversation && world.conversations.conversations.contains(conversation))
				for (auto &message : world.conversations.conversations.at(conversation).messages)
					if (std::find(receipts.begin(), receipts.end(), message.id) != receipts.end())
						for (const auto &recipient : message.recipients)
							if (!message.delivered.contains(recipient))
								message.expired.insert(recipient);
		}
		bool take_over(const InterdictionDefinition &d, EntityId boarder) override
		{
			if (!world.dock_ready(boarder, d.target))
				return false;
			if (d.repair_on_takeover)
				world.restore_repairable_ship(d.target);
			world.set_operational_owner(d.target, boarder);
			world.apply_occupation(d.target, d.occupation_teams, d.kill_original_crew);
			if (d.capture_visiting_personnel)
				world.capture_visitors(d.target, boarder);
			return true;
		}
		void publish(const InterdictionReceipt &receipt) override
		{
			switch (receipt.kind)
			{
			case InterdictionReceiptKind::Log:
				world.mission_transition("mission.log", receipt.player, receipt.target, receipt.encounter,
				                         receipt.text);
				break;
			case InterdictionReceiptKind::Taken:
				world.mission_transition("ship.taken", receipt.player, receipt.target, receipt.encounter, receipt.text);
				break;
			case InterdictionReceiptKind::Emit:
				if (world.scenario)
					world.scenario->runtime().emit(
					    {world.conversation_instance, ++world.conversation_event_sequence, 0}, receipt.text,
					    {{"ship", static_cast<std::int64_t>(receipt.player)}});
				break;
			}
		}
		void dock(EntityId ship, InterdictionDockOrder order, EntityId target) override
		{
			if (order == InterdictionDockOrder::Undock)
				world.encounter_dock_order(ship, "dock.undock");
			else
				world.encounter_dock_order(ship, "dock.assist", target);
		}
	};
	Native native(*this);
	interdiction_book.advance(native);
}

auto Runtime::charge_encounter_drive(EntityId ship) -> void
{
	const auto drive = departure_drive(ship);
	if (!drive)
		return;
	encounter_command(ship, OperatingAction::GeneratorOutput, std::numeric_limits<std::int64_t>::max());
	auto &controls = entities.at(ship).get_mut<EntityControls>().controllers;
	for (std::size_t i = 0; i < controls.size(); ++i)
	{
		auto &c = controls[i];
		if (c.human)
			continue;
		for (std::size_t j = 0; j < c.definition.commands.size(); ++j)
			if (c.definition.commands[j].action == OperatingAction::SystemEnabled)
				for (auto consumer : c.definition.commands[j].consumers)
					if (definition(ship).fitted->values[consumer].id == drive.get<Equipment>().id)
						commit_controller(
						    {{ship, c.definition.console, c.definition.id}, i, j, 1, c.generation, 0, false, {}});
	}
}

auto Runtime::prepare_encounter_departure(EntityId leader, const std::set<EntityId> &group) -> void
{
	for (const auto member : group)
	{
		charge_encounter_drive(member);
		if (entities.at(member).has<DockConnection>())
			encounter_dock_order(member, "dock.undock");
	}
	if (!departures.group_blocker(departure_admission_facts(), leader).empty())
		return;
	const auto drive = departure_drive(leader);
	if (!drive)
		return;
	const auto owner = ftl_owner(leader, drive.get<Equipment>().id);
	for (const auto &controller : entities.at(leader).get<EntityControls>().controllers)
		if (!controller.human && controller.definition.console == owner.console)
		{
			OperatingCommand command{leader, "ftl.depart", 0, drive.get<Equipment>().id};
			command.controller = controller.definition.id;
			command.generation = controller.generation;
			if (departures.admit(departure_admission_facts(), owner, command, true, true))
				return;
		}
}

auto Runtime::apply_interdiction_orders() -> void
{
	prune_departure_groups();
	for (std::size_t i = 0; i < interdiction_book.size(); ++i)
	{
		const auto &s = interdiction_book.state(i);
		const auto &d = interdiction_book.definition(i);
		const auto player = slot_entities.at(d.slot);
		InterdictionOrderFacts facts;
		facts.player = player;
		facts.player_position = entities.at(player).get<Pose>().position;
		facts.target_position = entities.at(d.target).get<Pose>().position;
		facts.target_captured = captured_by.contains(d.target);
		if (s.captured && facts.target_captured)
			facts.target_owner = operational_owner(d.target);
		facts.target_available = !truly_destroyed(d.target) && !departed(d.target);
		for (auto id : s.ships)
			if (!truly_destroyed(id) && !departed(id))
				facts.live_ships.push_back(id);
		if (const auto departure = interdiction_book.departure_order(i, facts))
		{
			release_encounter_navigation(departure->prize);
			if (departure->ready)
			{
				assign_departure_group(departure->leader, departure->group);
				encounter_move(departure->prize, departure->movement.destination, departure->movement.stop_radius,
				               departure->movement.cruise_speed);
				prepare_encounter_departure(departure->leader, departure->group);
			}
		}
		for (std::size_t n = 0; n < s.ships.size(); ++n)
		{
			const auto id = s.ships[n];
			if (truly_destroyed(id) || departed(id))
				continue;
			release_encounter_navigation(id);
			if (entities.at(id).has<DockConnection>())
				apply_docking_controller(entities.at(id));
			if (s.finished && entities.at(id).has<DockConnection>())
				encounter_dock_order(id, "dock.undock");
			facts.player_position = entities.at(player).get<Pose>().position;
			const InterdictionShipFacts ship{id, entities.at(id).get<Pose>().position, encounter_contact(id, player)};
			const auto order = interdiction_book.ship_order(i, facts, ship);
			encounter_command(id, OperatingAction::WeaponTarget, order.weapon_target);
			encounter_command(id, OperatingAction::GeneratorOutput, std::numeric_limits<std::int64_t>::max());
			if (order.hold_if_docked && entities.at(id).has<DockConnection>())
				continue;
			if (!order.attack)
				facts.player_definition = entities.at(player).get<Definition>().name;
			const auto movement = interdiction_book.ship_movement(i, n, facts, ship, order);
			encounter_move(id, movement.destination, movement.stop_radius, movement.cruise_speed);
		}
	}
	for (const auto &[leader, group] : departures.groups())
		for (const auto member : group)
			if (member != leader && !truly_destroyed(member) && !departed(member))
				charge_encounter_drive(member);
	// The same consumer used by ordinary Controller decisions actuates these admitted goals.
	for (const auto &[id, entity] : entities)
		if (!departed(id) && !truly_destroyed(id) && entity.has<EntityControls>())
			consume_controller_commands(entity);
}

auto Runtime::save_interdictions() const -> Json
{
	return interdiction_book.save();
}

auto Runtime::restore_interdictions(const Json &rows, const Catalog &content) -> void
{
	if (!rows.is_array() || rows.size() != interdiction_book.size())
		throw std::runtime_error("Invalid interdiction continuation");
	InterdictionRestoreFacts facts;
	for (const auto &[id, entity] : entities)
		facts.entities.insert(id);
	for (const auto &[id, owner] : captured_by)
		facts.captured_targets.insert(id);
	for (const auto &[id, conversation] : conversations.conversations)
	{
		auto &receipt = facts.conversations[id];
		receipt.program = conversation.program;
		for (const auto &message : conversation.messages)
			receipt.messages.insert(message.id);
	}
	for (std::size_t i = 0; i < rows.size(); ++i)
	{
		const auto &d = interdiction_book.definition(i);
		if (rows[i].at("continuation").at("program") != d.program.signature)
		{
			auto legacy_registry = interdiction_registry();
			legacy_registry.observations.pop_back();
			const auto legacy = state_tree::compile(content.trees.at(d.tree), legacy_registry, content.trees);
			facts.legacy_signatures.emplace(d.id, legacy.signature);
		}
	}
	interdiction_book.restore(rows, facts);
}

auto Runtime::validate_interdiction_physics() const -> void
{
	for (std::size_t i = 0; i < interdiction_book.size(); ++i)
	{
		const auto &s = interdiction_book.state(i);
		if (!s.captured && s.progress &&
		    (!s.boarder || s.blocked || !dock_ready(s.boarder, interdiction_book.definition(i).target)))
			throw std::runtime_error("Takeover progress without eligible physical docking");
	}
}
} // namespace phoenix::world_detail
