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
std::optional<Json> widget_datum(const Observation &view, const Json &controller, const WidgetReadoutBinding &binding,
                                 const std::string &equipment)
{
	if (binding.source == WidgetReadoutSource::Board)
	{
		return view.gameplay.contains(binding.pointer) ? std::optional<Json>(view.gameplay.at(binding.pointer))
		                                               : std::nullopt;
	}
	if (binding.source == WidgetReadoutSource::Command)
	{
		const auto commands = controller.value("commands", Json::object());
		const auto channel = commands.find(binding.field);
		if (channel != commands.end())
			return channel->at("value");
		return {};
	}
	if (binding.source == WidgetReadoutSource::System)
	{
		for (const auto &system : view.systems)
			if (system.id == equipment)
			{
				switch (binding.system_field)
				{
				case WidgetSystemReadout::Requested:
					return Json(system.requested);
				case WidgetSystemReadout::Maximum:
					return Json(system.maximum);
				case WidgetSystemReadout::Enabled:
					return Json(system.enabled);
				case WidgetSystemReadout::Status:
					return Json(system.status.empty() ? (system.enabled ? "Ready" : "Disabled") : system.status);
				case WidgetSystemReadout::EffectiveRange:
					return Json(system.effective_range);
				case WidgetSystemReadout::Telemetry:
					break;
				}
				const auto value = system.telemetry.find(binding.field);
				if (value != system.telemetry.end())
					return Json(value->second);
			}
	}
	else
	{
		if (!view.gameplay.contains(binding.rows))
			return {};
		const auto &rows = view.gameplay.at(binding.rows);
		Json values = Json::array();
		for (const auto &row : rows)
			if (row.at(binding.source == WidgetReadoutSource::Ftl ? "system" : "id") == equipment &&
			    row.contains(binding.pointer))
				values.push_back(
				    {{"label", row.value("contribution", std::string{})}, {"value", row.at(binding.pointer)}});
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
		const auto &readouts = catalog.widget_readouts.at(definition);
		ControlWidgetView widget;
		widget.id = controller.at("id").get<std::string>();
		widget.definition = definition;
		widget.automated = !controller.at("human").get<bool>();
		const auto placements =
		    controller.value("data_placement", Json::object()).value(std::to_string(stars), Json::object());
		const auto &data_items = catalog.widget_data.at(definition);
		for (const auto &[id, data] : data_items.items())
		{
			const auto placement = placements.value(id, data.value("placement", std::string("details")));
			if (placement == "hidden")
				continue;
			const auto destination = placement == "main" ? WidgetDataDestination::Main : WidgetDataDestination::Details;
			const auto &binding = readouts.at(id);
			auto append = [&](const std::string &identity, bool multiple)
			{
				ControlWidgetDatum datum;
				datum.id = id + (multiple ? "/" + identity : "");
				datum.label = localise(data.at("label").get<std::string>()) +
				              (multiple                  ? " / " + localise(identity)
				               : data.contains("member") ? " / " + localise(data.at("member").get<std::string>())
				                                         : "");
				datum.destination = destination;
				datum.equipment = identity;
				datum.binding = binding.display_path;
				const auto value = widget_datum(view, controller, binding, identity);
				if (data.contains("meter"))
				{
					const auto &meter = data.at("meter");
					const auto limit_id = meter.at("limit").get<std::string>();
					const auto limit_placement =
					    placements.value(limit_id, data_items.at(limit_id).value("placement", std::string("details")));
					datum.meter = meter.at("style").get<std::string>();
					if (value && value->is_number_integer())
						datum.quantity = value->get<std::int64_t>();
					if (limit_placement != "hidden")
						if (const auto limit = widget_datum(view, controller, readouts.at(limit_id), identity);
						    limit && limit->is_number_integer() && *limit > 0)
							datum.capacity = limit->get<std::int64_t>();
				}
				if (value && ((value->is_primitive() && !value->is_null()) || value->is_array()))
					datum.text = value->is_string() && value->get<std::string>().empty()
					                 ? localise(data.value("empty_text", std::string("—")))
					                 : datum_text(*value);
				else
					datum.text = localise("Unavailable");
				widget.data.push_back(std::move(datum));
			};
			if (binding.source == WidgetReadoutSource::Command || binding.source == WidgetReadoutSource::Board)
				append({}, false);
			else
			{
				const auto equipment = controller.value("equipment", Json::object());
				if (equipment.contains(binding.role))
				{
					const auto identities = equipment.at(binding.role).get<std::vector<std::string>>();
					for (const auto &identity : identities)
						append(identity, identities.size() > 1);
				}
			}
		}
		for (const auto &control : view.gameplay.value("controls", Json::array()))
		{
			const auto channel = control.value("widget_channel", control.value("channel", std::string{}));
			const auto field = std::find_if(source.at("commands").begin(), source.at("commands").end(),
			                                [&](const auto &value)
			                                {
				                                return value.at("id") == channel;
			                                });
			if (field != source.at("commands").end() && !field->value("expose", true))
				continue;
			if (control.value("widget_controller", control.value("controller", std::string{})) == widget.id &&
			    !channel.empty() && controller.at("commands").contains(channel) &&
			    controller.at("commands").at(channel).value("human", false))
				widget.controls.push_back(control);
			else if (control.value("widget_controller", control.value("controller", std::string{})) == widget.id &&
			         control.contains("operation") && console_operation_human(controller, control.at("operation")))
				widget.controls.push_back(control);
		}
		result.push_back(std::move(widget));
	}
	return result;
}
} // namespace phoenix
