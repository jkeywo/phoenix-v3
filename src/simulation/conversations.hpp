// Private World adapter. Domain state is typed; content is compiled only at load.
std::map<std::string, std::vector<CommsParticipant>> comms_rosters;
ConversationRuntime conversations;
bool comms_controller(EntityId id, const ConsoleControllerSpec &controller) const
{
	for (const auto &command : controller.commands)
		for (auto i : command.consumers)
			if (definition(id).fitted->values.at(i).kind == OperatingSystem::Comms)
				return true;
	return false;
}
std::optional<ConsoleAddress> comms_owner(EntityId id) const
{
	if (!entities.contains(id))
		return {};
	if (definition(id).layout)
		for (const auto &controller : definition(id).layout->controllers)
			if (comms_controller(id, controller))
				return ConsoleAddress{id, controller.console};
	for (const auto &s : definition(id).consoles)
		for (const auto &name : s.controlled_systems)
			for (const auto &fit : definition(id).fitted->values)
				if (fit.id == name && fit.kind == OperatingSystem::Comms)
					return ConsoleAddress{id, s.id};
	return {};
}
void register_comms(EntityId id, const std::string &definition)
{
	if (!comms_rosters.contains(definition))
		return;
	for (auto p : comms_rosters.at(definition))
	{
		p.host = id;
		p.id = "ship:" + std::to_string(id) + (p.character ? "/" + p.id : "");
		conversations.participants.emplace(p.id, std::move(p));
	}
}
void refresh_comms()
{
	conversations.endpoints.clear();
	for (const auto &[id, entity] : entities)
	{
		if (entity.has(flecs::Disabled))
			continue;
		auto owner = comms_owner(id);
		if (!owner)
			continue;
		const auto &pos = entity.get<Pose>().position;
		CommsEndpoint endpoint{id, pos.x, pos.y, 0, false, false};
		if (entity.has<EntityControls>())
			for (const auto &c : entity.get<EntityControls>().controllers)
				if (c.definition.console == owner->console && comms_controller(id, c.definition) && c.human)
					endpoint.human = true;
		for (auto child : entity.get<EquipmentIndex>().values)
		{
			const auto &fit = child.get<Equipment>();
			if (fit.kind != OperatingSystem::Comms)
				continue;
			const auto *power = entity.try_get<ShipResources>();
			const bool funded = power && child.has<ResourceBinding>() &&
			                    power->grants.at(child.get<ResourceBinding>().slot).supply_permille == 1000;
			if (fit.enabled && funded && !structurally_disabled(entity, fit.id) && !combat_dead(entity))
			{
				endpoint.working = true;
				endpoint.range = std::max(endpoint.range, officer_value(id, fit.id, "comms", fit.maximum) *
				                                              fault_effectiveness(entity, fit.id) / 1000);
			}
		}
		conversations.endpoints[id] = endpoint;
	}
	for (const auto &[id, endpoint] : conversations.endpoints)
	{
		const auto owner = comms_owner(id);
		const auto observed = capture_console_observation(*owner);
		std::set<EntityId> visible{id};
		for (const auto &c : observed.contacts)
			visible.insert(c.id);
		for (const auto &[name, p] : conversations.participants)
			if (visible.contains(p.host) && p.public_directory)
				conversations.disclose(id, name);
	}
}
bool comms_command(const ConsoleAddress &address, const OperatingCommand &command, bool execute)
{
	const auto owner = comms_owner(address.ship);
	if (!owner || *owner != address || !entities.at(address.ship).has<EntityControls>())
		return false;
	const auto &controllers = entities.at(address.ship).get<EntityControls>().controllers;
	if (std::none_of(controllers.begin(), controllers.end(),
	                 [&](const auto &c)
	                 {
		                 return c.definition.console == address.console &&
		                        comms_controller(address.ship, c.definition) && c.definition.id == command.controller &&
		                        c.human && c.generation == command.generation;
	                 }))
		return false;
	for (auto &[id, endpoint] : conversations.endpoints)
	{
		endpoint.human = false;
		if (entities.contains(id) && entities.at(id).has<EntityControls>())
			for (const auto &c : entities.at(id).get<EntityControls>().controllers)
				if (comms_controller(id, c.definition) && c.human)
					endpoint.human = true;
	}
	bool accepted = false;
	try
	{
		if (command.action == "comms.hail")
			accepted = conversations.hail(address.ship, command.text, execute) != 0;
		else if (command.action == "comms.text")
			accepted = conversations.send_text(address.ship, command.value, command.text, execute);
		else if (command.action == "comms.answer")
			accepted = conversations.answer(address.ship, command.value, command.request_id, command.text, execute);
		else if (command.action == "comms.select" && conversations.conversations.contains(command.value))
		{
			const auto &c = conversations.conversations.at(command.value);
			const auto self = "ship:" + std::to_string(address.ship);
			if (c.members.contains(self) || c.former.contains(self))
			{
				if (!execute)
					return true;
				conversations.selected[address.ship] = command.value;
				for (const auto &m : c.messages)
					if (m.delivered.contains(self))
						conversations.read_through[address.ship][c.id] =
						    std::max(conversations.read_through[address.ship][c.id], m.id);
				accepted = true;
			}
		}
	}
	catch (const std::exception &)
	{
		return false;
	}
	return accepted;
}
struct ConversationStart
{
	std::string id, tree, title, start_event;
	struct Binding
	{
		EntityId entity = 0;
		std::string slot, character;
	};
	std::map<std::string, Binding> bindings;
	std::vector<std::string> members, events;
	ConversationId conversation = 0;
	ScenarioId cursor = 0;
};
std::vector<ConversationStart> conversation_starts;
ScenarioId conversation_instance = 0, conversation_event_sequence = 0;
void compile_conversation_starts(const Json &map)
{
	for (const auto &j : map.value("conversations", Json::array()))
	{
		ConversationStart s;
		s.id = j.at("id");
		s.tree = j.at("tree");
		s.title = j.value("title_key", std::string{});
		s.start_event = j.value("start_event", std::string{});
		s.members = j.at("members").get<std::vector<std::string>>();
		s.events = j.value("events", std::vector<std::string>{});
		for (auto it = j.at("bindings").begin(); it != j.at("bindings").end(); ++it)
			s.bindings[it.key()] = {it.value().value("entity", EntityId{0}), it.value().value("slot", std::string{}),
			                        it.value().value("character", std::string{})};
		conversation_starts.push_back(std::move(s));
	}
	if (scenario && !comms_rosters.empty())
		conversation_instance = scenario->runtime().create_instance("comms-events");
}
void conversation_scenarios()
{
	for (auto &s : conversation_starts)
	{
		std::vector<ScenarioEvent> events;
		if (scenario)
		{
			std::set<std::string> names(s.events.begin(), s.events.end());
			names.insert(s.start_event);
			for (const auto &name : names)
				for (const auto &e : scenario->runtime().events(name, s.cursor))
					events.push_back(e);
			std::sort(events.begin(), events.end(),
			          [](const auto &a, const auto &b)
			          {
				          return a.id < b.id;
			          });
		}
		bool start = s.start_event.empty() || std::any_of(events.begin(), events.end(),
		                                                  [&](const auto &e)
		                                                  {
			                                                  return e.name == s.start_event;
		                                                  });
		if (!s.conversation && (start || boarding_conversation_requested(s.id)))
		{
			std::map<std::string, std::string> bindings;
			bool available = true;
			for (const auto &[alias, b] : s.bindings)
			{
				const auto entity =
				    b.entity ? b.entity : (slot_entities.contains(b.slot) ? slot_entities.at(b.slot) : 0);
				const auto id = "ship:" + std::to_string(entity) + (b.character.empty() ? "" : "/" + b.character);
				if (!conversations.participants.contains(id))
				{
					available = false;
					break;
				}
				bindings[alias] = id;
			}
			if (available)
			{
				std::vector<std::string> members;
				for (const auto &alias : s.members)
					members.push_back(bindings.at(alias));
				s.conversation = conversations.open(s.tree, std::move(bindings), members, s.title);
			}
		}
		if (s.conversation)
			for (const auto &e : events)
				if (std::find(s.events.begin(), s.events.end(), e.name) != s.events.end())
					conversations.event(s.conversation, e.name);
		if (!events.empty() && (s.conversation || !start))
			s.cursor = events.back().id;
	}
	if (scenario && conversation_instance)
	{
		for (const auto &[id, event] : conversations.emitted)
			scenario->runtime().emit({conversation_instance, ++conversation_event_sequence, 0}, event,
			                         {{"conversation", static_cast<std::int64_t>(id)}});
		conversations.emitted.clear();
	}
}
Json save_conversation_starts() const
{
	Json rows = Json::array();
	for (const auto &s : conversation_starts)
		rows.push_back({{"id", s.id}, {"conversation", s.conversation}, {"cursor", s.cursor}});
	return {{"instance", conversation_instance}, {"sequence", conversation_event_sequence}, {"starts", rows}};
}
void restore_conversation_starts(const Json &j)
{
	conversation_instance = j.at("instance");
	conversation_event_sequence = j.at("sequence");
	if (j.at("starts").size() != conversation_starts.size())
		throw std::runtime_error("Saved conversation starts mismatch");
	std::vector<ConversationStart> ordered;
	std::set<std::string> identities;
	for (const auto &row : j.at("starts"))
	{
		const auto id = row.at("id").get<std::string>();
		const auto found = std::find_if(conversation_starts.begin(), conversation_starts.end(),
		                                [&](const auto &s)
		                                {
			                                return s.id == id;
		                                });
		if (!identities.insert(id).second || found == conversation_starts.end())
			throw std::runtime_error("Saved conversation configuration mismatch");
		ordered.push_back(*found);
	}
	conversation_starts = std::move(ordered);
	for (std::size_t i = 0; i < conversation_starts.size(); ++i)
	{
		auto &s = conversation_starts[i];
		const auto &v = j.at("starts")[i];
		if (v.at("id") != s.id)
			throw std::runtime_error("Saved conversation configuration mismatch");
		s.conversation = v.at("conversation");
		s.cursor = v.at("cursor");
		if (s.conversation && !conversations.conversations.contains(s.conversation))
			throw std::runtime_error("Missing saved scenario conversation");
	}
}
