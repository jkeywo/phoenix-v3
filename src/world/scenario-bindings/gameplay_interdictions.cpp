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
	std::set<EntityId> existing;
	for (const auto &[id, entity] : entities)
		existing.insert(id);
	for (const auto &ship : interdiction_book.restore_ships(rows, bodies, existing))
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
				facts.dock_ready = facts.boarder_alive && world.dock_ready(s.boarder, d.target);
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
		void bind_dialogue_attacker(std::uint64_t conversation, const std::string &participant) override
		{
			auto &thread = world.conversations.conversations.at(conversation);
			auto &attacker = thread.bindings.at("pirate");
			if (attacker == participant)
				return;
			if (!world.conversations.participants.contains(participant))
				throw std::runtime_error("Unknown interdiction attacker participant");
			thread.members.erase(attacker);
			thread.former.insert(attacker);
			thread.members.insert(participant);
			thread.former.erase(participant);
			attacker = participant;
			// Existing messages retain their snapshotted sender, name and portrait.
			for (const auto &member : thread.members)
			{
				const auto &person = world.conversations.participants.at(member);
				if (!person.character)
					world.conversations.disclose(person.host, participant);
			}
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
	};
	Native native(*this);
	interdiction_book.advance(native);
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
		if (s.working && (!s.boarder || !dock_ready(s.boarder, interdiction_book.definition(i).target)))
			throw std::runtime_error("Takeover progress without eligible physical docking");
	}
}
} // namespace phoenix::world_detail
