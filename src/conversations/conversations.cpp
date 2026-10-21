module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <map>
#include <set>
#include <vector>
module phoenix.conversations;
namespace phoenix
{
namespace
{
void require(bool ok, const std::string &why)
{
	if (!ok)
		throw std::runtime_error("conversations: " + why);
}
std::string ship_id(EntityId id)
{
	return "ship:" + std::to_string(id);
}
} // namespace
void ConversationRuntime::disclose(EntityId viewer, const std::string &id)
{
	require(participants.contains(id), "unknown disclosed participant");
	known[viewer].insert(id);
}
bool ConversationRuntime::linked(const std::string &a, const std::string &b) const
{
	if (!participants.contains(a) || !participants.contains(b))
		return false;
	const auto ah = participants.at(a).host, bh = participants.at(b).host;
	if (!endpoints.contains(ah) || !endpoints.contains(bh))
		return false;
	const auto &x = endpoints.at(ah), &y = endpoints.at(bh);
	if (!x.working || !y.working)
		return false;
	const auto dx = x.x - y.x, dy = x.y - y.y, r = std::min(x.range, y.range);
	const auto dz = x.altitude - y.altitude;
	// World coordinates are bounded to +/-1e9, so the sum fits signed 64 bits.
	return r >= 0 && std::abs(dx) <= r && std::abs(dy) <= r && std::abs(dz) <= r &&
	       dx * dx + dy * dy + dz * dz <= r * r;
}
ConversationId ConversationRuntime::open(const std::string &program, std::map<std::string, std::string> bindings,
                                         const std::vector<std::string> &members, const std::string &title)
{
	require(conversations.size() < 256 && members.size() >= 2 && members.size() <= 32, "conversation capacity");
	require(program.empty() || programs.contains(program), "unknown conversation program");
	for (const auto &[alias, id] : bindings)
		require(participants.contains(id), "unknown bound participant " + id);
	Conversation c;
	c.id = next_id_;
	c.program = program;
	c.bindings = std::move(bindings);
	c.title = title.empty() && !program.empty() ? programs.at(program).title : title;
	for (const auto &id : members)
	{
		require(participants.contains(id), "unknown member");
		c.members.insert(id);
	}
	require(c.members.size() >= 2, "conversation requires distinct participants");
	for (const auto &to : c.members)
		if (!participants.at(to).character)
			for (const auto &member : c.members)
				disclose(participants.at(to).host, member);
	++next_id_;
	auto id = c.id;
	conversations.emplace(id, std::move(c));
	return id;
}
ConversationId ConversationRuntime::hail(EntityId source, const std::string &target, bool execute)
{
	const auto from = ship_id(source);
	if (!known.contains(source) || !known.at(source).contains(target) || from == target || !linked(from, target))
		return 0;
	const auto key = std::min(from, target) + "|" + std::max(from, target);
	if (direct_.contains(key))
	{
		auto &c = conversations.at(direct_.at(key));
		if (!execute)
			return c.id;
		c.members.insert(from);
		c.members.insert(target);
		++c.hails;
		selected[source] = c.id;
		return c.id;
	}
	const auto &p = participants.at(target);
	std::map<std::string, std::string> bindings{{"caller", from}, {"target", target}};
	for (const auto &[id, person] : participants)
		if (person.host == p.host && person.character)
			bindings.try_emplace(id.substr(id.find('/') + 1), id);
	const auto program = !p.character && endpoints.at(p.host).human ? std::string{} : p.default_tree;
	if (conversations.size() >= 256 || (!program.empty() && !programs.contains(program)))
		return 0;
	if (!execute)
		return next_id_;
	const auto id = open(program, std::move(bindings), {from, target}, p.name);
	direct_[key] = id;
	conversations.at(id).hails = 1;
	selected[source] = id;
	return id;
}
bool ConversationRuntime::text_allowed(const Conversation &c) const
{
	return !c.members.empty() && std::all_of(c.members.begin(), c.members.end(),
	                                         [&](const auto &id)
	                                         {
		                                         const auto &p = participants.at(id);
		                                         return !p.character && endpoints.contains(p.host) &&
		                                                endpoints.at(p.host).human;
	                                         });
}
ConversationId ConversationRuntime::message(Conversation &c, const std::string &sender, const std::string &body,
                                            std::uint64_t delay, std::uint64_t incoming, std::uint64_t expiry,
                                            bool literal)
{
	require(c.members.contains(sender), "speaker is not a member");
	require(c.messages.size() < 4096, "transcript capacity reached");
	const auto &p = participants.at(sender);
	ConversationMessage m;
	m.id = next_id_++;
	m.sender = sender;
	m.name = p.name;
	m.portrait = p.portrait;
	m.text = body;
	m.sent = tick;
	m.due = tick + delay + incoming;
	m.incoming = incoming;
	m.expires = expiry ? m.due + expiry : 0;
	m.literal = literal;
	m.recipients = c.members;
	c.messages.push_back(std::move(m));
	return c.messages.back().id;
}
bool ConversationRuntime::send_text(EntityId source, ConversationId id, const std::string &body, bool execute)
{
	if (!conversations.contains(id) || body.empty() || body.size() > 4096 || !text_allowed(conversations.at(id)))
		return false;
	auto &c = conversations.at(id);
	auto from = ship_id(source);
	if (!c.members.contains(from) || c.messages.size() >= 4096)
		return false;
	if (execute)
		message(c, from, body, 0, 0, 0, true);
	return true;
}
void ConversationRuntime::resolve(Conversation &c, const std::string &who, const std::string &choice)
{
	auto &p = c.prompt;
	const auto &d = programs.at(c.program).prompts.at(p.definition);
	auto found = std::find_if(d.choices.begin(), d.choices.end(),
	                          [&](const auto &v)
	                          {
		                          return v.id == choice;
	                          });
	require(found != d.choices.end(), "unknown answer");
	p.answer = choice;
	p.respondent = who;
	if (!who.empty())
		message(c, who, found->text);
}
bool ConversationRuntime::answer(EntityId source, ConversationId id, ConversationId prompt, const std::string &choice,
                                 bool execute)
{
	if (!conversations.contains(id))
		return false;
	auto &c = conversations.at(id);
	auto &p = c.prompt;
	const auto who = ship_id(source);
	if (!p.id || p.id != prompt || !p.answer.empty() || !p.eligible.contains(who) || !c.members.contains(who) ||
	    (p.deadline && tick >= p.deadline))
		return false;
	const auto m = std::find_if(c.messages.begin(), c.messages.end(),
	                            [&](const auto &v)
	                            {
		                            return v.id == p.message;
	                            });
	if (m == c.messages.end() || !m->delivered.contains(who) || !linked(who, m->sender))
		return false;
	const auto &d = programs.at(c.program).prompts.at(p.definition);
	if (std::none_of(d.choices.begin(), d.choices.end(),
	                 [&](const auto &v)
	                 {
		                 return v.id == choice;
	                 }))
		return false;
	if (c.messages.size() >= 4096)
		return false;
	if (execute)
		resolve(c, who, choice);
	return true;
}
bool ConversationRuntime::report(ConversationId id, const std::string &line)
{
	auto found = conversations.find(id);
	if (found == conversations.end())
		return false;
	auto &c = found->second;
	const auto &p = programs.at(c.program);
	const auto at = p.lines.find(line);
	if (at == p.lines.end() || !c.bindings.contains(at->second.speaker))
		return false;
	const auto &l = at->second;
	message(c, c.bindings.at(l.speaker), l.text, l.delay, l.incoming, l.expiry);
	return true;
}
void ConversationRuntime::event(ConversationId id, const std::string &name)
{
	require(conversations.contains(id) && conversations.at(id).events.size() < 256,
	        "event queue full/unknown conversation");
	conversations.at(id).events.push_back(name);
}
void ConversationRuntime::step(Conversation &c)
{
	if (c.program.empty() || c.finished)
		return;
	using state_tree::Value;
	const auto &p = programs.at(c.program);
	auto continuation = c.continuation;
	std::vector<Value> obs{Value::integer(tick),
	                       Value::string(c.prompt.answer),
	                       Value::string(c.prompt.respondent),
	                       Value::string(c.events.empty() ? "" : c.events.front()),
	                       Value::integer(c.hails),
	                       Value::boolean(c.prompt.id && c.prompt.answer.empty())};
	const auto outputs = state_tree::step(p.tree, continuation, obs);
	// Stage only mutable script state and newly appended messages. Existing history
	// remains in place until every output has been validated.
	Conversation candidate{
	    c.id,     c.title, c.program, c.bindings, c.members, c.former, {}, c.prompt, std::move(continuation),
	    c.events, c.hails, c.finished};
	std::set<std::string> removed;
	std::vector<std::pair<EntityId, std::string>> disclosures;
	std::vector<std::pair<ConversationId, std::string>> emissions;
	if (!candidate.events.empty())
		candidate.events.erase(candidate.events.begin());
	const auto old_id = next_id_;
	try
	{
		for (const auto &o : outputs.outputs)
		{
			const auto name = p.tree.registry.actions.at(o.action).name;
			const auto ref = o.arguments.at(0).text;
			const auto member = [&](const std::string &alias)
			{
				require(candidate.bindings.contains(alias), "unbound participant " + alias);
				require(participants.contains(candidate.bindings.at(alias)), "unknown bound participant " + alias);
				return candidate.bindings.at(alias);
			};
			if (name == "say")
			{
				const auto &l = p.lines.at(ref);
				message(candidate, member(l.speaker), l.text, l.delay, l.incoming, l.expiry);
			}
			else if (name == "offer")
			{
				require(!candidate.prompt.id || !candidate.prompt.answer.empty(), "a prompt is already pending");
				const auto &d = p.prompts.at(ref);
				auto sender = member(d.speaker);
				auto mid = message(candidate, sender, d.text);
				candidate.prompt = {next_id_++, mid, ref, "", "", d.timeout ? tick + d.timeout : 0, tick, {}};
				for (const auto &id : candidate.members)
					if (!participants.at(id).character && id != sender)
						candidate.prompt.eligible.insert(id);
			}
			else if (name == "add_participant")
			{
				require(candidate.members.size() < 32, "participant capacity");
				candidate.members.insert(member(ref));
				for (const auto &to : candidate.members)
					if (!participants.at(to).character)
						for (const auto &participant : candidate.members)
							disclosures.emplace_back(participants.at(to).host, participant);
			}
			else if (name == "remove_participant")
			{
				auto id = member(ref);
				candidate.members.erase(id);
				candidate.former.insert(id);
				removed.insert(id);
				for (auto &m : candidate.messages)
					if (m.recipients.contains(id) && !m.delivered.contains(id))
						m.expired.insert(id);
			}
			else if (name == "reveal")
			{
				auto id = member(ref);
				for (const auto &to : candidate.members)
					if (!participants.at(to).character)
						disclosures.emplace_back(participants.at(to).host, id);
			}
			else if (name == "emit")
				emissions.emplace_back(c.id, ref);
			else if (name == "finish")
				candidate.finished = true;
			require(c.messages.size() + candidate.messages.size() <= 4096, "transcript capacity reached");
		}
		for (const auto &id : removed)
			for (auto &m : c.messages)
				if (m.recipients.contains(id) && !m.delivered.contains(id))
					m.expired.insert(id);
		for (auto &m : candidate.messages)
			c.messages.push_back(std::move(m));
		candidate.messages = std::move(c.messages);
		for (const auto &[viewer, participant] : disclosures)
			disclose(viewer, participant);
		emitted.insert(emitted.end(), emissions.begin(), emissions.end());
		c = std::move(candidate);
	}
	catch (...)
	{
		next_id_ = old_id;
		throw;
	}
}
void ConversationRuntime::advance(std::uint64_t now)
{
	require(now >= tick, "time moved backwards");
	tick = now;
	for (auto &[id, c] : conversations)
	{
		for (auto &m : c.messages)
			for (const auto &to : m.recipients)
			{
				if (m.delivered.contains(to) || m.expired.contains(to))
					continue;
				if (!c.members.contains(to) || (m.expires && tick >= m.expires))
				{
					m.expired.insert(to);
					continue;
				}
				if (tick >= m.due && linked(m.sender, to))
					m.delivered.insert(to);
			}
		if (c.prompt.id && c.prompt.answer.empty())
		{
			const auto &d = programs.at(c.program).prompts.at(c.prompt.definition);
			if (c.prompt.deadline && tick >= c.prompt.deadline)
				resolve(c, "", d.default_choice);
			else if (!d.ai_choice.empty() && tick >= c.prompt.opened + d.ai_delay)
				for (const auto &who : c.prompt.eligible)
				{
					const auto &p = participants.at(who);
					if (endpoints.contains(p.host) && !endpoints.at(p.host).human &&
					    answer(p.host, id, c.prompt.id, d.ai_choice))
						break;
				}
		}
		step(c);
	}
}
Json ConversationRuntime::project(EntityId viewer) const
{
	const auto self = ship_id(viewer);
	Json out{{"directory", Json::array()},
	         {"threads", Json::array()},
	         {"selected", selected.contains(viewer) ? selected.at(viewer) : 0}};
	if (known.contains(viewer))
		for (const auto &id : known.at(viewer))
			if (participants.contains(id))
			{
				const auto &p = participants.at(id);
				out["directory"].push_back({{"id", id},
				                            {"name", p.name},
				                            {"portrait", p.portrait},
				                            {"host", p.host},
				                            {"available", linked(self, id)}});
			}
	for (const auto &[id, c] : conversations)
	{
		if (!c.members.contains(self) && !c.former.contains(self))
			continue;
		Json row{{"id", id},
		         {"title", c.title},
		         {"members", Json::array()},
		         {"messages", Json::array()},
		         {"incoming", Json::array()},
		         {"choices", Json::array()},
		         {"prompt", 0},
		         {"text_allowed", c.members.contains(self) && text_allowed(c)},
		         {"unread", false}};
		// Only current membership is shown to current participants.
		if (c.members.contains(self))
			for (const auto &m : c.members)
			{
				const auto &p = participants.at(m);
				row["members"].push_back(
				    {{"id", m}, {"name", p.name}, {"portrait", p.portrait}, {"connected", linked(self, m)}});
			}
		for (const auto &m : c.messages)
		{
			if (m.delivered.contains(self))
			{
				row["messages"].push_back({{"id", m.id},
				                           {"sender", m.sender},
				                           {"name", m.name},
				                           {"portrait", m.portrait},
				                           {"text", m.text},
				                           {"literal", m.literal},
				                           {"own", m.sender == self}});
				if (!read_through.contains(viewer) || !read_through.at(viewer).contains(c.id) ||
				    m.id > read_through.at(viewer).at(c.id))
					row["unread"] = true;
			}
			else if (m.recipients.contains(self) && !m.expired.contains(self) && c.members.contains(self) &&
			         m.incoming && tick >= m.due - m.incoming && tick < m.due && linked(m.sender, self))
				row["incoming"].push_back({{"sender", m.sender}, {"name", m.name}});
		}
		const auto &p = c.prompt;
		if (p.id && p.answer.empty() && p.eligible.contains(self) && c.members.contains(self) &&
		    (!p.deadline || tick < p.deadline))
		{
			const auto m = std::find_if(c.messages.begin(), c.messages.end(),
			                            [&](const auto &v)
			                            {
				                            return v.id == p.message;
			                            });
			if (m != c.messages.end() && m->delivered.contains(self) && linked(self, m->sender))
			{
				row["prompt"] = p.id;
				for (const auto &v : programs.at(c.program).prompts.at(p.definition).choices)
					row["choices"].push_back({{"id", v.id}, {"text", v.text}});
			}
		}
		out["threads"].push_back(std::move(row));
	}
	return out;
}
Json ConversationRuntime::save() const
{
	Json out{{"tick", tick},         {"next", next_id_},     {"direct", direct_},        {"known", known},
	         {"selected", selected}, {"read", read_through}, {"threads", Json::array()}, {"emitted", emitted}};
	for (const auto &[id, c] : conversations)
	{
		Json j{{"id", id},
		       {"title", c.title},
		       {"program", c.program},
		       {"bindings", c.bindings},
		       {"members", c.members},
		       {"former", c.former},
		       {"hails", c.hails},
		       {"events", c.events},
		       {"finished", c.finished},
		       {"messages", Json::array()}};
		const auto &p = c.prompt;
		j["prompt"] = {{"id", p.id},         {"message", p.message},       {"definition", p.definition},
		               {"answer", p.answer}, {"respondent", p.respondent}, {"deadline", p.deadline},
		               {"opened", p.opened}, {"eligible", p.eligible}};
		if (!c.program.empty())
			j["continuation"] = state_tree::save(programs.at(c.program).tree, c.continuation);
		for (const auto &m : c.messages)
			j["messages"].push_back({{"id", m.id},
			                         {"sender", m.sender},
			                         {"name", m.name},
			                         {"portrait", m.portrait},
			                         {"text", m.text},
			                         {"sent", m.sent},
			                         {"due", m.due},
			                         {"incoming", m.incoming},
			                         {"expires", m.expires},
			                         {"literal", m.literal},
			                         {"recipients", m.recipients},
			                         {"delivered", m.delivered},
			                         {"expired", m.expired}});
		out["threads"].push_back(std::move(j));
	}
	return out;
}
void ConversationRuntime::restore(const Json &j)
{
	auto candidate = *this;
	candidate.conversations.clear();
	candidate.tick = j.at("tick");
	candidate.next_id_ = j.at("next");
	candidate.direct_ = j.at("direct").get<decltype(direct_)>();
	candidate.known = j.at("known").get<decltype(known)>();
	candidate.selected = j.at("selected").get<decltype(selected)>();
	candidate.read_through = j.at("read").get<decltype(read_through)>();
	candidate.emitted = j.at("emitted").get<decltype(emitted)>();
	require(j.at("threads").is_array() && j.at("threads").size() <= 256, "saved thread bounds");
	std::set<ConversationId> ids;
	const auto check_id = [&](ConversationId id)
	{
		require(id && id < candidate.next_id_ && ids.insert(id).second, "invalid saved identity");
	};
	for (const auto &v : j.at("threads"))
	{
		Conversation c;
		c.id = v.at("id");
		check_id(c.id);
		c.title = v.at("title");
		c.program = v.at("program");
		c.bindings = v.at("bindings").get<decltype(c.bindings)>();
		c.members = v.at("members").get<decltype(c.members)>();
		c.former = v.at("former").get<decltype(c.former)>();
		c.hails = v.at("hails");
		c.events = v.at("events").get<decltype(c.events)>();
		c.finished = v.at("finished");
		require(c.members.size() <= 32 && c.events.size() <= 256 && v.at("messages").size() <= 4096,
		        "saved conversation bounds");
		for (const auto &id : c.members)
			require(participants.contains(id), "saved unknown member");
		for (const auto &[alias, id] : c.bindings)
			require(participants.contains(id), "saved unknown binding");
		if (!c.program.empty())
		{
			require(programs.contains(c.program), "saved unknown tree");
			c.continuation = state_tree::load(programs.at(c.program).tree, v.at("continuation"));
		}
		const auto &p = v.at("prompt");
		c.prompt = {
		    p.at("id"),         p.at("message"),  p.at("definition"), p.at("answer"),
		    p.at("respondent"), p.at("deadline"), p.at("opened"),     p.at("eligible").get<std::set<std::string>>()};
		if (c.prompt.id)
		{
			check_id(c.prompt.id);
			require(programs.contains(c.program) && programs.at(c.program).prompts.contains(c.prompt.definition),
			        "saved unknown prompt");
		}
		for (const auto &m : v.at("messages"))
		{
			ConversationMessage r{m.at("id"),
			                      m.at("sender"),
			                      m.at("name"),
			                      m.at("portrait"),
			                      m.at("text"),
			                      m.at("sent"),
			                      m.at("due"),
			                      m.at("incoming"),
			                      m.at("expires"),
			                      m.at("literal"),
			                      m.at("recipients").get<std::set<std::string>>(),
			                      m.at("delivered").get<std::set<std::string>>(),
			                      m.at("expired").get<std::set<std::string>>()};
			check_id(r.id);
			require(participants.contains(r.sender) && r.text.size() <= 4096 && r.sent <= candidate.tick &&
			            r.due >= r.sent && r.incoming <= r.due - r.sent,
			        "saved invalid message");
			for (const auto &to : r.recipients)
				require(participants.contains(to), "saved unknown recipient");
			for (const auto &to : r.delivered)
				require(r.recipients.contains(to) && !r.expired.contains(to), "saved invalid delivery");
			for (const auto &to : r.expired)
				require(r.recipients.contains(to), "saved invalid expiry");
			c.messages.push_back(std::move(r));
		}
		if (c.prompt.id)
			require(std::any_of(c.messages.begin(), c.messages.end(),
			                    [&](const auto &m)
			                    {
				                    return m.id == c.prompt.message;
			                    }),
			        "saved prompt message missing");
		candidate.conversations.emplace(c.id, std::move(c));
	}
	for (const auto &[key, id] : candidate.direct_)
		require(candidate.conversations.contains(id), "saved direct thread missing");
	for (const auto &[ship, id] : candidate.selected)
		require(!id || candidate.conversations.contains(id), "saved selected thread missing");
	for (const auto &[ship, list] : candidate.known)
		for (const auto &id : list)
			require(participants.contains(id), "saved known participant missing");
	*this = std::move(candidate);
}
} // namespace phoenix
