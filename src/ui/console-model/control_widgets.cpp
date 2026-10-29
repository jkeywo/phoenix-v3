module;
#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

module phoenix.console;
import phoenix.application.preferences;
import phoenix.console_controls;

namespace phoenix
{
namespace
{
std::optional<Json> widget_datum(const Observation &view, const Json &controller, const std::string &binding,
                                 const std::string &equipment)
{
	if (binding.starts_with("board:/"))
	{
		const Json::json_pointer pointer(binding.substr(6));
		return view.gameplay.contains(pointer) ? std::optional<Json>(view.gameplay.at(pointer)) : std::nullopt;
	}
	if (binding.starts_with("command."))
	{
		const auto commands = controller.value("commands", Json::object());
		const auto channel = commands.find(binding.substr(8));
		if (channel != commands.end())
			return channel->at("value");
		return {};
	}
	const auto separator = binding.find('.');
	if (separator == std::string::npos)
		return {};
	const auto source = binding.substr(0, separator);
	const auto field = binding.substr(separator + 1);
	if (source == "system")
	{
		for (const auto &system : view.systems)
			if (system.id == equipment)
			{
				if (field == "requested")
					return Json(system.requested);
				if (field == "maximum")
					return Json(system.maximum);
				if (field == "enabled")
					return Json(system.enabled);
				if (field == "status")
					return Json(system.status.empty() ? (system.enabled ? "Ready" : "Disabled") : system.status);
				if (field == "effective_range")
					return Json(system.effective_range);
				const auto value = system.telemetry.find(field);
				if (value != system.telemetry.end())
					return Json(value->second);
			}
	}
	else if (source == "weapon" || source == "team" || source == "ftl" || source == "shield")
	{
		const auto rows = source == "weapon" || source == "shield"
		                      ? view.gameplay.value("combat", Json::object())
		                            .value(source == "shield" ? "shields" : "weapons", Json::array())
		                      : view.gameplay.value(source == "team" ? "teams" : "ftl", Json::array());
		Json values = Json::array();
		for (const auto &row : rows)
			if (row.at(source == "ftl" ? "system" : "id") == equipment && row.contains(field))
				values.push_back({{"label", row.value("contribution", std::string{})}, {"value", row.at(field)}});
		if (values.size() == 1)
			return values[0].at("value");
		if (!values.empty())
			return values;
	}
	return {};
}
std::string datum_text(const Json &value)
{
	if (value.is_string())
		return localise(value.get<std::string>());
	if (value.is_boolean())
		return localise(value.get<bool>() ? "On" : "Off");
	if (value.is_number())
		return value.dump();
	if (value.is_array())
	{
		std::string text;
		for (const auto &member : value)
			if (member.is_object() && member.contains("label") && member.at("label").is_string() &&
			    member.contains("value"))
			{
				if (!text.empty())
					text += "; ";
				text += localise(member.at("label").get<std::string>()) + ": " + datum_text(member.at("value"));
			}
		return text;
	}
	return {};
}
} // namespace

std::vector<ControlWidgetView> console_control_widgets(const Catalog &catalog, const Observation &view)
{
	std::vector<ControlWidgetView> result;
	const auto profile = view.gameplay.value("profile", std::string{});
	const auto stars = view.gameplay.value("profile_stars", Json::object()).value(profile, 0);
	for (const auto &controller : view.gameplay.value("controllers", Json::array()))
	{
		const auto definition = controller.value("widget", std::string{});
		if (definition.empty() || !catalog.control_widgets.contains(definition))
			continue;
		const auto &source = catalog.control_widgets.at(definition);
		ControlWidgetView widget;
		widget.id = controller.at("id").get<std::string>();
		widget.definition = definition;
		for (const auto &[channel, command] : controller.at("commands").items())
			widget.automated &= !command.value("human", false);
		for (const auto &operation : controller.value("operations", Json::object()))
			widget.automated &= !operation.value("human", false);
		const auto placements =
		    controller.value("data_placement", Json::object()).value(std::to_string(stars), Json::object());
		const auto data_items = source.value("data", Json::object());
		for (const auto &[id, data] : data_items.items())
		{
			const auto placement = placements.value(id, data.value("placement", std::string("details")));
			if (placement == "hidden")
				continue;
			const auto destination = placement == "main" ? WidgetDataDestination::Main : WidgetDataDestination::Details;
			const auto binding = data.at("binding").get<std::string>();
			auto append = [&](const std::string &identity, const std::string &path, bool multiple)
			{
				ControlWidgetDatum datum;
				datum.id = id + (multiple ? "/" + identity : "");
				datum.label = localise(data.at("label").get<std::string>()) +
				              (multiple                  ? " / " + localise(identity)
				               : data.contains("member") ? " / " + localise(data.at("member").get<std::string>())
				                                         : "");
				datum.destination = destination;
				const auto value = widget_datum(view, controller, path, identity);
				if (value && ((value->is_primitive() && !value->is_null()) || value->is_array()))
					datum.text = value->is_string() && value->get<std::string>().empty()
					                 ? localise(data.value("empty_text", std::string("—")))
					                 : datum_text(*value);
				else
					datum.text = localise("Unavailable");
				widget.data.push_back(std::move(datum));
			};
			if (binding.starts_with("command.") || binding.starts_with("board:/"))
				append({}, binding, false);
			else
			{
				const auto equipment = controller.value("equipment", Json::object());
				std::string matched_role;
				for (const auto &[role, identities] : equipment.items())
					if (role.size() > matched_role.size() &&
					    (binding.starts_with("system." + role + ".") || binding.starts_with("weapon." + role + ".") ||
					     binding.starts_with("team." + role + ".") || binding.starts_with("ftl." + role + ".") ||
					     binding.starts_with("shield." + role + ".")))
						matched_role = role;
				if (!matched_role.empty())
				{
					const auto source_end = binding.find('.');
					const auto path =
					    binding.substr(0, source_end + 1) + binding.substr(source_end + matched_role.size() + 2);
					const auto identities = equipment.at(matched_role).get<std::vector<std::string>>();
					for (const auto &identity : identities)
						append(identity, path, identities.size() > 1);
				}
			}
		}
		for (const auto &control : view.gameplay.value("controls", Json::array()))
		{
			const auto channel = control.value("widget_channel", control.value("channel", std::string{}));
			if (control.value("widget_controller", control.value("controller", std::string{})) == widget.id &&
			    !channel.empty() && controller.at("commands").contains(channel) &&
			    controller.at("commands").at(channel).value("human", false))
				widget.controls.push_back(control);
			else if (control.value("controller", std::string{}) == widget.id && control.contains("operation") &&
			         console_operation_human(controller, control.at("operation")))
				widget.controls.push_back(control);
		}
		result.push_back(std::move(widget));
	}
	return result;
}
} // namespace phoenix
