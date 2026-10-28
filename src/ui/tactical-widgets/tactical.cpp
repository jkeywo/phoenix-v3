module;
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.ui.tactical_widgets;
import phoenix.content.catalog;
import phoenix.content.controllers;

import phoenix.ui.engineering_widgets;
import phoenix.application.preferences;
namespace phoenix
{
void TacticalWidgets::refresh(const ConsoleModel &primary_model, const std::shared_ptr<SessionPort> &session,
                              ConsoleId route)
{
	console_.clear();
	const auto *layout = primary_model.layout();
	if (!layout || !std::any_of(layout->at("widgets").begin(), layout->at("widgets").end(),
	                            [](const auto &widget)
	                            {
		                            return widget.at("kind") == "throttle";
	                            }))
		return;
	const auto primary = session->console_snapshot(route);
	if (!primary)
		return;
	for (const auto &spec : resolve_consoles(session->catalog(), primary->own.definition))
	{
		const auto &definition = session->catalog().consoles.at(spec.definition);
		const auto &widgets = definition.at("widgets");
		if (widgets.size() != 1 || widgets[0].at("kind") != "radar")
			continue;
		for (const auto &extra : primary->additional_consoles)
			if (extra.console == spec.id &&
			    !extra.gameplay.value("combat", Json::object()).value("weapons", Json::array()).empty())
			{
				model_.refresh(session, route);
				if (!model_.observation() || model_.observation()->console != extra.console)
				{
					model_.act("game.console/" + extra.console);
					model_.refresh(session, route);
				}
				console_ = extra.console;
				return;
			}
	}
}
std::vector<std::pair<std::string, std::string>> TacticalWidgets::weapon_controls() const
{
	std::vector<std::pair<std::string, std::string>> result;
	if (console_.empty() || !model_.observation())
		return result;
	const auto &view = *model_.observation();
	for (const auto &system : view.systems)
		if (system.kind == OperatingSystem::Weapon)
			for (const auto &control : view.gameplay.value("controls", Json::array()))
				if (control.value("action", "") == "system_enabled" &&
				    control.value("value", -1) == (system.enabled ? 0 : 1))
				{
					const auto consumers = command_consumers(view, control);
					if (std::find(consumers.begin(), consumers.end(), system.id) == consumers.end())
						continue;
					result.emplace_back("inline/" + descriptor_action(control.at("id")),
					                    engineering_name(system.id) + ": " +
					                        localise(system.enabled ? "Enabled" : "Disabled"));
					break;
				}
	return result;
}
std::vector<std::pair<std::string, std::string>> TacticalWidgets::target_readouts(ConsoleModel &primary_model,
                                                                                  const Catalog &catalog, bool detailed)
{
	std::vector<std::pair<std::string, std::string>> result;
	auto &target_model = console_.empty() ? primary_model : model_;
	const auto view = primary_model.observation();
	if (!view)
		return result;
	const auto selected = target_model.sensor_contact();
	result.emplace_back("target/title", localise("Selected target"));
	bool found = false;
	for (const auto &contact : view->contacts)
		if (contact.id == selected)
		{
			found = true;
			const auto name = catalog.entities.contains(contact.definition)
			                      ? catalog.entities.at(contact.definition).value("name", contact.definition)
			                      : contact.definition;
			result.emplace_back("target/name", localise(name) + " #" + std::to_string(contact.id));
			result.emplace_back(
			    "target/range",
			    localise("Range") + ": " +
			        std::to_string(static_cast<std::int64_t>(std::max(0.0, std::hypot(double(contact.x - view->own.x),
			                                                                          double(contact.y - view->own.y)) -
			                                                                   contact.radius)) /
			                       1000) +
			        " m");
			if (detailed)
				result.emplace_back(
				    "target/speed",
				    localise("Speed") + ": " +
				        std::to_string(static_cast<std::int64_t>(std::hypot(double(contact.vx), double(contact.vy))) /
				                       1000) +
				        " m/s");
			result.emplace_back("target/faction",
			                    localise(contact.faction_label.empty() ? "Unaffiliated" : contact.faction_label) +
			                        " / " + localise(contact.disposition));
		}
	for (const auto &port : primary_model.docking().value("ports", Json::array()))
		if (selected && port.at("target") == selected && port.at("phase") != "free")
		{
			const auto phase = port.at("phase").get<std::string>();
			result.emplace_back("target/docking", localise(phase == "approach"     ? "Approaching docking port"
			                                               : phase == "mating"     ? "Connecting"
			                                               : phase == "separating" ? "Undocking"
			                                                                       : "Docked"));
		}
	if (!found)
		result.emplace_back("target/empty", localise(selected ? "Selected contact is outside positional sensor coverage"
		                                                      : "Select a contact on the radar"));
	std::vector<std::string> blocked_progress;
	if (target_model.observation())
		for (const auto &scanner :
		     target_model.observation()->gameplay.value("sensors", Json::object()).value("scanners", Json::array()))
			if (!scanner.value("blocker", std::string{}).empty())
				blocked_progress.push_back("game.scan-progress/" + scanner.at("id").get<std::string>() + "/");
	for (const auto &[key, text] : target_model.gameplay_readouts())
		if (detailed && selected &&
		    std::none_of(blocked_progress.begin(), blocked_progress.end(),
		                 [&](const auto &prefix)
		                 {
			                 return key.starts_with(prefix);
		                 }) &&
		    !key.starts_with("game.scan-finding/" + std::to_string(selected) + "/basic_identity") &&
		    !key.starts_with("game.scan-finding/" + std::to_string(selected) + "/contact_arcs") &&
		    (key.starts_with("game.scan-finding/") || key.starts_with("game.scan-status/") ||
		     key.starts_with("game.scan-progress/")))
			result.emplace_back("target/" + key, text);
	return result;
}
bool TacticalWidgets::select_contact(ConsoleModel &primary_model, EntityId contact)
{
	const bool accepted = console_.empty() ? primary_model.select_contact(contact) : model_.select_contact(contact);
	primary_model.act("game.sensor-contact/" + std::to_string(contact));
	return accepted;
}
} // namespace phoenix
