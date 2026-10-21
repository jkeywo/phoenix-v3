module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
module phoenix.content;
namespace phoenix
{
void verify_conversation_authoring(const Catalog &catalog)
{
	if (catalog.presentation)
		return;
	auto require = [](bool ok, const std::string &path, const std::string &why)
	{
		if (!ok)
			throw std::runtime_error(path + ": " + why);
	};
	auto tree = [&](const std::string &name, const std::string &path)
	{
		require(name.empty() || (catalog.tree_programs.contains(name) &&
		                         catalog.tree_programs.at(name).context == state_tree::Context::Conversation),
		        path, "expected a conversation tree");
	};

	auto references = [&](const std::string &name, const std::set<std::string> &aliases, const std::string &path)
	{
		std::set<std::string> emissions;
		if (name.empty())
			return emissions;
		const auto compiled = compile_conversation(catalog.trees.at(name), catalog.trees);
		for (const auto &[key, line] : compiled.lines)
			require(aliases.contains(line.speaker), path, "unbound line speaker " + line.speaker);
		for (const auto &[key, prompt] : compiled.prompts)
			require(aliases.contains(prompt.speaker), path, "unbound prompt speaker " + prompt.speaker);
		const auto walk = [&](const auto &self, const state_tree::Program &program) -> void
		{
			for (const auto &state : program.states)
				for (const auto *tasks : {&state.entry, &state.running, &state.exit})
					for (const auto &task : *tasks)
						if (task.kind == state_tree::TaskKind::Action)
						{
							const auto action = program.registry.actions.at(task.target).name;
							require(task.arguments.at(0).source == state_tree::Source::Literal, path,
							        "conversation action references must be literal names");
							const auto ref = task.arguments.at(0).literal.text;
							if (action == "add_participant" || action == "remove_participant" || action == "reveal")
								require(aliases.contains(ref), path, "unbound participant " + ref);
							if (action == "emit")
								emissions.insert(ref);
						}
			for (const auto &child : program.references)
				self(self, child);
		};
		walk(walk, compiled.tree);
		return emissions;
	};
	std::map<std::string, std::set<std::string>> hull_emissions;
	for (const auto &[id, entity] : catalog.entities)
	{
		if (!entity.contains("comms"))
			continue;
		const auto path = "entities." + id + ".comms";
		const auto &c = entity.at("comms");
		const auto fits = resolve_systems(catalog, id);
		require(std::any_of(fits.begin(), fits.end(),
		                    [](const auto &s)
		                    {
			                    return s.kind == OperatingSystem::Comms;
		                    }),
		        path, "directory requires fitted Comms");
		tree(c.value("default_conversation", std::string{}), path);
		auto people = c.value("characters", Json::array());
		require(people.is_array() && people.size() <= 64, path, "expected at most 64 characters");
		std::set<std::string> ids;
		for (const auto &p : people)
		{
			auto name = p.at("id").get<std::string>();
			require(valid_identifier(name) && ids.insert(name).second, path, "invalid or repeated character identity");
			require(p.contains("name_key") && p.at("name_key").is_string() &&
			            !p.at("name_key").get_ref<const std::string &>().empty() &&
			            p.at("name_key").get_ref<const std::string &>().size() <= 4096,
			        path, "character requires name_key");
			tree(p.value("default_conversation", std::string{}), path + "." + name);
			const auto portrait = p.value("portrait", std::string{});
			require(portrait.empty() || catalog.assets.contains(portrait), path, "unknown portrait asset");
		}
		std::set<std::string> aliases = ids;
		aliases.insert("caller");
		aliases.insert("target");
		auto &emissions = hull_emissions[id];
		auto include = [&](const std::string &name)
		{
			auto result = references(name, aliases, path);
			emissions.insert(result.begin(), result.end());
		};
		include(c.value("default_conversation", std::string{}));
		for (const auto &person : people)
			include(person.value("default_conversation", std::string{}));
	}
	for (const auto &[id, map] : catalog.maps)
	{
		const auto path = "maps." + id + ".conversations";
		auto starts = map.value("conversations", Json::array());
		require(starts.is_array() && starts.size() <= 128, path, "expected at most 128 conversations");
		const auto declared = [&](const std::string &event)
		{
			require(catalog.scenarios.contains(id) &&
			            std::find(catalog.scenarios.at(id).events.begin(), catalog.scenarios.at(id).events.end(),
			                      event) != catalog.scenarios.at(id).events.end(),
			        path, "undeclared scenario event " + event);
		};
		std::set<std::string> hulls;
		for (const auto &entity : map.value("entities", Json::array()))
			hulls.insert(entity.at("definition"));
		for (const auto &slot : map.value("ship_slots", Json::array()))
			for (const auto &hull : slot.at("allowed_ships"))
				hulls.insert(hull.get<std::string>());
		for (const auto &hull : hulls)
			for (const auto &event : hull_emissions[hull])
				declared(event);
		std::set<std::string> ids;
		for (const auto &c : starts)
		{
			auto name = c.at("id").get<std::string>();
			require(valid_identifier(name) && ids.insert(name).second, path, "invalid or repeated configuration");
			const auto program = c.at("tree").get<std::string>();
			require(c.value("title_key", std::string{}).size() <= 4096, path, "title exceeds projection bound");
			tree(program, path);
			require(!program.empty(), path, "missing tree");
			const auto &bindings = c.at("bindings");
			require(bindings.is_object() && bindings.size() <= 64, path, "invalid bindings");
			for (auto b = bindings.begin(); b != bindings.end(); ++b)
			{
				require(valid_identifier(b.key()) && b.value().contains("entity") != b.value().contains("slot"), path,
				        "binding requires entity or slot");
				std::vector<std::string> hulls;
				if (b.value().contains("entity"))
				{
					for (const auto &e : map.at("entities"))
						if (e.at("id") == b.value().at("entity"))
							hulls.push_back(e.at("definition"));
				}
				else
					for (const auto &s : map.at("ship_slots"))
						if (s.at("id") == b.value().at("slot"))
							hulls = s.at("allowed_ships").get<std::vector<std::string>>();
				require(!hulls.empty(), path, "unknown bound entity/slot");
				for (const auto &h : hulls)
				{
					const auto &e = catalog.entities.at(h);
					auto fits = resolve_systems(catalog, h);
					require(std::any_of(fits.begin(), fits.end(),
					                    [](const auto &s)
					                    {
						                    return s.kind == OperatingSystem::Comms;
					                    }),
					        path, "participant has no Comms");
					const auto character = b.value().value("character", std::string{});
					if (!character.empty())
					{
						auto chars = e.value("comms", Json::object()).value("characters", Json::array());
						require(std::any_of(chars.begin(), chars.end(),
						                    [&](const auto &p)
						                    {
							                    return p.at("id") == character;
						                    }),
						        path, "unknown bound character");
					}
				}
			}
			require(c.at("members").is_array() && c.at("members").size() >= 2 && c.at("members").size() <= 32, path,
			        "expected 2..32 initial participants");
			std::set<std::string> members;
			for (const auto &m : c.at("members"))
			{
				auto alias = m.get<std::string>();
				require(bindings.contains(alias) && members.insert(alias).second, path,
				        "unknown or repeated member binding");
			}
			auto events = c.value("events", std::vector<std::string>{});
			const auto start = c.value("start_event", std::string{});
			if (!start.empty())
				events.push_back(start);
			for (const auto &event : events)
				require(catalog.scenarios.contains(id) &&
				            std::find(catalog.scenarios.at(id).events.begin(), catalog.scenarios.at(id).events.end(),
				                      event) != catalog.scenarios.at(id).events.end(),
				        path, "undeclared scenario event " + event);
			std::set<std::string> aliases;
			for (auto b = bindings.begin(); b != bindings.end(); ++b)
				aliases.insert(b.key());
			for (const auto &event : references(program, aliases, path))
				declared(event);
		}
	}
}
} // namespace phoenix
