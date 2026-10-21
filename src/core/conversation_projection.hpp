#pragma once
#include "body_projection.hpp"
namespace phoenix::conversation_projection
{

// Client admission is based on the current permitted projection; World rechecks at commit.
inline bool command_allowed(const Observation &view, const OperatingCommand &command)
{
	if (!command.action.starts_with("comms.") || command.target != view.own.id || !command.system.empty() ||
	    !command.channel.empty() || !command.arguments.empty() || command.text.size() > 4096 ||
	    !view.gameplay.contains("conversations"))
		return false;
	bool authority = false;
	for (const auto &section : view.gameplay.value("controllers", Json::array()))
		if (section.at("id") == command.controller && section.value("human", false) &&
		    section.at("generation") == command.generation)
			for (const auto &channel : section.at("commands"))
				for (const auto &consumer : channel.at("consumers"))
					for (const auto &system : view.systems)
						if (system.kind == OperatingSystem::Comms && system.id == consumer.get<std::string>())
							authority = true;
	if (!authority)
		return false;
	const auto &board = view.gameplay.at("conversations");
	if (command.action == "comms.hail" && command.value == 0 && !command.request_id)
		for (const auto &p : board.at("directory"))
			if (p.at("id") == command.text && p.at("available") == true)
				return true;
	for (const auto &thread : board.at("threads"))
		if (thread.at("id") == command.value)
		{
			if (command.action == "comms.select")
				return command.text.empty() && !command.request_id;
			if (command.action == "comms.text")
				return !command.text.empty() && !command.request_id && thread.at("text_allowed") == true;
			if (command.action == "comms.answer" && command.request_id && thread.at("prompt") == command.request_id)
				for (const auto &choice : thread.at("choices"))
					if (choice.at("id") == command.text)
						return true;
		}
	return false;
}
inline Json thread(const Json &v, bool shared = false, std::size_t message_limit = 4096)
{
	using projection::number;
	using projection::text;
	if (v.empty())
		return Json::object();
	Json out{{"id", number(v.at("id"), 1)},
	         {"title", text(v.at("title"), 4096)},
	         {"members", Json::array()},
	         {"messages", Json::array()},
	         {"incoming", Json::array()}};
	if (v.at("members").size() > 32 || v.at("messages").size() > 4096 || v.at("incoming").size() > 4096)
		throw std::runtime_error("Conversation projection bounds");
	for (const auto &p : v.at("members"))
		out["members"].push_back({{"id", text(p.at("id"))},
		                          {"name", text(p.at("name"), 4096)},
		                          {"portrait", text(p.at("portrait"))},
		                          {"connected", p.at("connected").get<bool>()}});
	for (const auto &p : v.at("incoming"))
		out["incoming"].push_back({{"sender", text(p.at("sender"))}, {"name", text(p.at("name"), 4096)}});
	out["omitted_messages"] = v.value("omitted_messages", std::uint64_t{0});
	std::size_t bytes = out.dump().size();
	for (auto it = v.at("messages").rbegin(); it != v.at("messages").rend(); ++it)
	{
		if (out["messages"].size() >= message_limit || bytes > 256 * 1024)
		{
			out["omitted_messages"] = out["omitted_messages"].get<std::uint64_t>() +
			                          static_cast<std::uint64_t>(std::distance(it, v.at("messages").rend()));
			break;
		}
		const auto &m = *it;
		Json row{{"id", number(m.at("id"), 1)},      {"sender", text(m.at("sender"))},
		         {"name", text(m.at("name"), 4096)}, {"portrait", text(m.at("portrait"))},
		         {"text", text(m.at("text"), 4096)}, {"literal", m.at("literal").get<bool>()},
		         {"own", m.at("own").get<bool>()}};
		bytes += row.dump().size();
		if (bytes <= 256 * 1024)
			out["messages"].push_back(std::move(row));
		else
			out["omitted_messages"] = out["omitted_messages"].get<std::uint64_t>() + 1;
	}
	std::reverse(out["messages"].begin(), out["messages"].end());
	if (!shared)
	{
		out["unread"] = v.at("unread").get<bool>();
		out["text_allowed"] = v.at("text_allowed").get<bool>();
		out["prompt"] = number(v.at("prompt"), 0);
		out["choices"] = Json::array();
		if (v.at("choices").size() > 16)
			throw std::runtime_error("Conversation choices bounds");
		for (const auto &c : v.at("choices"))
			out["choices"].push_back({{"id", text(c.at("id"))}, {"text", text(c.at("text"), 4096)}});
	}
	return out;
}
inline Json board(const Json &v)
{
	using projection::number;
	using projection::text;
	if (v.empty())
		return Json::object();
	if (v.at("threads").size() > 256 || v.at("directory").size() > 4096)
		throw std::runtime_error("Conversation directory bounds");
	Json out{{"selected", number(v.at("selected"), 0)}, {"threads", Json::array()}, {"directory", Json::array()}};
	for (const auto &p : v.at("directory"))
		out["directory"].push_back({{"id", text(p.at("id"))},
		                            {"name", text(p.at("name"), 4096)},
		                            {"portrait", text(p.at("portrait"))},
		                            {"host", number(p.at("host"), 1)},
		                            {"available", p.at("available").get<bool>()}});
	for (const auto &t : v.at("threads"))
	{
		out["threads"].push_back(thread(t, false, t.at("id") == out.at("selected") ? 4096 : 1));
	}
	if (out.dump().size() > 768 * 1024)
		throw std::runtime_error("Conversation metadata exceeds projection budget");
	return out;
}
} // namespace phoenix::conversation_projection
