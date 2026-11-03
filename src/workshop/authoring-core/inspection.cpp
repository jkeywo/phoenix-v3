module;
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
module phoenix.workshop.authoring_core;
import phoenix.content.ui;
namespace phoenix
{
Json workshop_layout_palette(const WorkshopDocument &document, const Json &console, bool header,
                             const std::string &requested)
{
	Json targets = Json::array(), contexts = Json::object();
	const auto catalog = document.preview_ui();
	for (const auto &component : console_ui_components())
		if (header == (component.id == "settings" || component.id == "console-name" || component.id == "tabs"))
			targets.push_back(component.id);
	if (!header)
	{
		for (const auto &widget : console.at("widgets"))
			targets.push_back(widget.at("id"));
		for (const auto &[ship_id, ship] : catalog->entities)
			if (!ship.contains("_instance_template"))
				for (const auto &station : ship.value("stations", Json::array()))
					for (const auto &seat : station.at("consoles"))
						if (seat.value("definition", std::string{}) == console.at("id").get<std::string>())
							contexts[ship_id + "/" + seat.at("id").get<std::string>()] = ship_id;
	}
	const auto context = contexts.contains(requested) ? requested
	                     : contexts.empty()           ? std::string{}
	                                                  : contexts.begin().key();
	if (!context.empty())
	{
		const auto &ship = catalog->entities.at(contexts.at(context).get<std::string>());
		const auto seat = context.substr(context.find('/') + 1);
		for (const auto &instance : ship.value("control_layout", Json::object()).value("controllers", Json::array()))
			if (instance.value("console", std::string{}) == seat && instance.contains("widget"))
			{
				const auto prefix = "control/" + instance.at("id").get<std::string>() + "/";
				targets.push_back(prefix + "complete");
				const auto definition = catalog->control_widgets.find(instance.at("widget").get<std::string>());
				if (definition != catalog->control_widgets.end())
					for (const auto &part : definition->second.value("parts", Json::array({"main"})))
						targets.push_back(prefix + part.get<std::string>());
			}
	}
	return {{"targets", targets}, {"contexts", contexts}, {"context", context}};
}
Json workshop_instance_fields(const WorkshopDocument &document, const std::set<std::uint64_t> &ids)
{
	std::vector<Json> effective;
	std::set<std::uint64_t> found;
	for (const auto &instance : document.map().at("entities"))
		if (ids.contains(instance.at("id").get<std::uint64_t>()))
		{
			found.insert(instance.at("id").get<std::uint64_t>());
			auto value = document.preview_definition(WorkshopDefinitionKind::Entity, instance.at("definition"));
			value.merge_patch(instance.value("overrides", Json::object()));
			value["heading_mdeg"] = instance.at("heading_mdeg");
			if (!value.contains("faction"))
				value["faction"] = "";
			effective.push_back(value);
		}
	if (found != ids)
		throw std::runtime_error("Unknown selected instance");
	if (effective.empty())
		return Json::array();
	std::set<std::string> sprites;
	bool first_sprite = true;
	for (const auto &entity : effective)
	{
		std::set<std::string> choices;
		const auto render = entity.value("render", Json::object());
		if (render.contains("sprite"))
		{
			choices.insert("");
			for (const auto &entry : render.at("sprite").value("entries", Json::array()))
				choices.insert(entry.at("name").get<std::string>());
		}
		if (first_sprite)
			sprites = choices;
		else
			std::erase_if(sprites,
			              [&](const std::string &id)
			              {
				              return !choices.contains(id);
			              });
		first_sprite = false;
	}
	std::function<Json(const std::vector<Json> &, const std::string &, bool)> describe;
	describe = [&](const std::vector<Json> &values, const std::string &path, bool direct) -> Json
	{
		const auto &first = values.front();
		Json result = Json::array();
		if (first.is_object())
		{
			for (const char *field : {"kind", "mode", "shape"})
				if (first.contains(field) && std::any_of(values.begin(), values.end(),
				                                         [&](const Json &row)
				                                         {
					                                         return !row.contains(field) ||
					                                                row.at(field) != first.at(field);
				                                         }))
					return result;
			for (const auto &[key, value] : first.items())
			{
				if (key == "id" || key == "extends" || key == "kind" || key == "mode" || key == "shape" ||
				    key.starts_with("_") || (key == "sprite_entry" && sprites.empty()))
					continue;
				std::vector<Json> common;
				for (const auto &row : values)
					if (row.is_object() && row.contains(key) &&
					    (row.at(key).type() == value.type() ||
					     (row.at(key).is_number_integer() && value.is_number_integer())))
						common.push_back(row.at(key));
				if (common.size() != values.size() || value.is_array())
					continue;
				const auto child = path + (Json::json_pointer("") / key).to_string();
				auto entries =
				    describe(common, child,
				             path.empty() && (key == "heading_mdeg" || key == "faction" || key == "sprite_entry"));
				if (value.is_object())
					result.push_back({{"label", key}, {"path", child}, {"children", entries}});
				else
					for (auto &entry : entries)
						result.push_back(std::move(entry));
			}
		}
		else
		{
			const auto title = path.substr(path.find_last_of('/') + 1);
			Json spec{{"label", title},
			          {"path", path},
			          {"direct", direct},
			          {"value", first},
			          {"mixed", std::any_of(values.begin(), values.end(),
			                                [&](const Json &value)
			                                {
				                                return value != first;
			                                })}};
			if (title == "sprite_entry")
				spec["choices"] = sprites;
			result.push_back(spec);
		}
		return result;
	};
	return describe(effective, "", false);
}
void workshop_instance_field(WorkshopDocument &document, const std::set<std::uint64_t> &ids, const std::string &path,
                             const Json &value)
{
	const auto fields = workshop_instance_fields(document, ids);
	Json selected;
	std::function<void(const Json &)> find = [&](const Json &rows)
	{
		for (const auto &row : rows)
		{
			if (row.contains("children"))
				find(row.at("children"));
			else if (row.at("path") == path)
				selected = row;
		}
	};
	find(fields);
	if (selected.is_null())
		throw std::runtime_error("Field is not common to the selected instances");
	const auto &before = selected.at("value");
	if ((before.is_string() && !value.is_string()) || (before.is_boolean() && !value.is_boolean()) ||
	    (before.is_number_integer() && selected.at("label") != "scale" && !value.is_number_integer()) ||
	    (before.is_number_float() && !value.is_number()))
		throw std::runtime_error("Wrong bulk field type");
	if (selected.contains("choices") &&
	    std::find(selected.at("choices").begin(), selected.at("choices").end(), value) == selected.at("choices").end())
		throw std::runtime_error("Sprite entry is not common to the selected instances");
	Json patch = Json::object(), direct = Json::object();
	if (selected.at("direct").get<bool>())
		direct[selected.at("label").get<std::string>()] = value;
	else
		patch[Json::json_pointer(path)] = value;
	document.edit_instances(ids, direct, patch);
}
} // namespace phoenix
