module;
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.content.ui;

namespace phoenix
{
Json console_help_level(const Json &console, int stars)
{
	if (!console.contains("help") || !console.at("help").contains("levels"))
		return Json::object();
	const auto &levels = console.at("help").at("levels");
	if (stars == 0 && console.at("help").contains("default_level"))
		stars = console.at("help").at("default_level").get<int>();
	return levels.value(std::to_string(stars), Json::object());
}
std::vector<std::pair<std::string, std::string>> console_help_systems(const Json &level, const std::string &page)
{
	std::vector<std::pair<std::string, std::string>> result;
	if (!level.contains("systems"))
		return result;
	if (page.empty())
	{
		for (const auto &system : level.at("systems"))
			result.emplace_back(system.at("label"), system.at("text"));
		return result;
	}
	if (!level.at("tabs").contains(page))
		return result;
	for (const auto &id : level.at("tabs").at(page))
		for (const auto &system : level.at("systems"))
			if (system.at("id") == id)
				result.emplace_back(system.at("label"), system.at("text"));
	return result;
}
void verify_console_help(const Catalog &catalog, const Json &console)
{
	if (!console.contains("help"))
		return;
	const auto where = console.at("id").get<std::string>() + ".help";
	auto check = [&](bool valid, const std::string &message)
	{
		if (!valid)
			throw std::runtime_error(where + ": " + message);
	};
	auto text = [](const Json &value)
	{
		return value.is_string() && !value.get<std::string>().empty();
	};
	const auto &help = console.at("help");
	check(help.is_object() && help.contains("levels") && help.at("levels").is_object() && !help.at("levels").empty(),
	      "expected nonempty levels object");
	if (help.contains("default_level"))
	{
		const auto &level = help.at("default_level");
		check(level.is_number_integer() && level.get<int>() >= 1 && level.get<int>() <= 3 &&
		          help.at("levels").contains(std::to_string(level.get<int>())),
		      "invalid default level");
	}
	std::set<std::string> pages{"$console"};
	if (console.contains("layout"))
	{
		const auto layout = compile_ui_layout(catalog, console);
		std::function<void(const UiNode &)> collect = [&](const UiNode &node)
		{
			pages.insert(node.id);
			for (const auto &child : node.children)
			{
				if (node.kind == "tabs")
					pages.insert(child.id);
				collect(child);
			}
		};
		collect(layout.root);
		if (layout.compact)
			collect(*layout.compact);
	}
	for (const auto &[key, level] : help.at("levels").items())
	{
		check(key == "1" || key == "2" || key == "3", "level must be 1, 2 or 3");
		check(level.is_object() && level.contains("responsibilities") && text(level.at("responsibilities")) &&
		          level.contains("automation") && text(level.at("automation")),
		      "expected responsibilities and automation text");
		check(level.contains("systems") && level.at("systems").is_array() && !level.at("systems").empty(),
		      "expected nonempty systems array");
		std::set<std::string> ids;
		for (const auto &system : level.at("systems"))
		{
			check(system.is_object() && system.contains("id") && text(system.at("id")) && system.contains("label") &&
			          text(system.at("label")) && system.contains("text") && text(system.at("text")),
			      "system requires id, label and text");
			check(ids.insert(system.at("id").get<std::string>()).second, "duplicate system id");
		}
		check(level.contains("tabs") && level.at("tabs").is_object() && !level.at("tabs").empty(),
		      "expected tabs object");
		for (const auto &[page, systems] : level.at("tabs").items())
		{
			check(pages.contains(page), "unknown help page " + page);
			check(systems.is_array() && !systems.empty(), "tab requires system references");
			std::set<std::string> used;
			for (const auto &id : systems)
				check(text(id) && ids.contains(id.get<std::string>()) && used.insert(id.get<std::string>()).second,
				      "invalid or duplicate tab system reference");
		}
	}
	if (help.contains("required_levels"))
	{
		check(help.at("required_levels").is_array() && !help.at("required_levels").empty(),
		      "required_levels must be a nonempty array");
		for (const auto &stars : help.at("required_levels"))
		{
			check(stars.is_number_integer() && stars.get<int>() >= 1 && stars.get<int>() <= 3,
			      "invalid required level");
			const auto key = std::to_string(stars.get<int>());
			check(help.at("levels").contains(key), "missing required level " + key);
			for (const auto &page : pages)
				check(help.at("levels").at(key).at("tabs").contains(page), "missing help for page " + page);
		}
	}
}
} // namespace phoenix
