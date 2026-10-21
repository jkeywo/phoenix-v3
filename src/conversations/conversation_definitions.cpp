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
std::string text(const Json &j, const char *key, bool required = true)
{
	auto value = j.value(key, std::string{});
	require(value.size() <= 4096 && (!required || !value.empty()), std::string("invalid ") + key);
	return value;
}
std::uint64_t duration(const Json &j, const char *key, std::uint64_t fallback = 0)
{
	if (!j.contains(key))
		return fallback;
	require(j.at(key).is_number_integer() && j.at(key) >= 0 && j.at(key) <= 1728000, std::string("invalid ") + key);
	return j.at(key).get<std::uint64_t>();
}
} // namespace
state_tree::Registry conversation_registry()
{
	using namespace state_tree;
	Registry r;
	for (const auto &[name, type] : std::vector<std::pair<std::string, Type>>{{"tick", Type::Integer},
	                                                                          {"answer", Type::Text},
	                                                                          {"respondent", Type::Text},
	                                                                          {"event", Type::Text},
	                                                                          {"hails", Type::Integer},
	                                                                          {"prompt_pending", Type::Boolean}})
		r.observations.push_back({name, type, 8, {}});
	for (const auto &name : {"say", "offer", "add_participant", "remove_participant", "reveal", "emit", "finish"})
		r.actions.push_back({name, 8, {Type::Text}, true});
	return r;
}
ConversationProgram compile_conversation(const Json &input, const state_tree::Catalog &catalog)
{
	auto j = state_tree::resolve_definition(input, catalog);
	ConversationProgram out;
	out.title = text(j, "title_key", false);
	out.tree = state_tree::compile(input, conversation_registry(), catalog);
	require(out.tree.context == state_tree::Context::Conversation, "expected conversation tree");
	auto lines = j.value("lines", Json::object());
	auto prompts = j.value("prompts", Json::object());
	require(lines.is_object() && prompts.is_object() && lines.size() <= 1024 && prompts.size() <= 256,
	        "definition bounds");
	for (auto it = lines.begin(); it != lines.end(); ++it)
		out.lines.emplace(it.key(),
		                  ConversationLine{text(it.value(), "speaker"), text(it.value(), "text_key"),
		                                   duration(it.value(), "delay_ticks"), duration(it.value(), "incoming_ticks"),
		                                   duration(it.value(), "expires_after")});
	for (auto it = prompts.begin(); it != prompts.end(); ++it)
	{
		const auto &p = it.value();
		ConversationPromptDefinition d;
		d.speaker = text(p, "speaker");
		d.text = text(p, "text_key");
		d.timeout = duration(p, "timeout_ticks");
		d.default_choice = text(p, "default_choice", false);
		d.ai_choice = text(p, "ai_choice", false);
		d.ai_delay = duration(p, "ai_delay_ticks", 20);
		require(p.at("choices").is_array() && !p.at("choices").empty() && p.at("choices").size() <= 16,
		        "choices must contain 1..16 entries");
		std::set<std::string> ids;
		for (const auto &c : p.at("choices"))
		{
			auto id = text(c, "id");
			require(id.size() <= 256 && ids.insert(id).second, "invalid or duplicate choice identity");
			d.choices.push_back({id, text(c, "text_key")});
		}
		require((!d.timeout && d.default_choice.empty()) || (d.timeout && ids.contains(d.default_choice)),
		        "timeout requires a valid default choice");
		require(d.ai_choice.empty() || ids.contains(d.ai_choice), "invalid AI choice");
		out.prompts.emplace(it.key(), std::move(d));
	}
	// Literal action references are checked before play, including referenced trees.
	const auto validate = [&](const auto &self, const state_tree::Program &p) -> void
	{
		for (const auto &s : p.states)
			for (const auto *tasks : {&s.entry, &s.running, &s.exit})
				for (const auto &t : *tasks)
					if (t.kind == state_tree::TaskKind::Action && !t.arguments.empty() &&
					    t.arguments[0].source == state_tree::Source::Literal)
					{
						const auto name = p.registry.actions.at(t.target).name;
						const auto ref = t.arguments[0].literal.text;
						if (name == "say")
							require(out.lines.contains(ref), "unknown line " + ref);
						if (name == "offer")
							require(out.prompts.contains(ref), "unknown prompt " + ref);
					}
		for (const auto &p2 : p.references)
			self(self, p2);
	};
	validate(validate, out.tree);
	return out;
}
} // namespace phoenix
