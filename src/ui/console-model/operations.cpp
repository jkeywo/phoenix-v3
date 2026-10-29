module;
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <iomanip>
#include <string>
#include <utility>
#include <tuple>
#include <vector>
#include <nlohmann/json.hpp>

module phoenix.console;

import phoenix.application.preferences;
import phoenix.console_controls;
#include "model_internal.hpp"
namespace phoenix
{
using namespace console_detail;
Json ConsoleModel::away_missions() const
{
	return observation_ ? observation_->gameplay.value("away_missions", Json::array()) : Json::array();
}
Json ConsoleModel::logistics() const
{
	return observation_ ? observation_->gameplay.value("logistics", Json::object()) : Json::object();
}
Json ConsoleModel::docking() const
{
	return observation_ ? observation_->gameplay.value("docking", Json::object()) : Json::object();
}
bool ConsoleModel::selection_action(const std::string &kind, const std::string &system, const std::string &choice)
{
	if (!observation_)
		return false;
	if (kind != "magazine" && kind != "processor")
		return false;
	const auto rows = kind == "magazine"
	                      ? observation_->gameplay.value("combat", Json::object()).value("magazines", Json::array())
	                      : observation_->gameplay.value("processors", Json::array());
	for (const auto &row : rows)
		if (row.at("id") == system)
			for (const auto &controller : controller_rows(observation_->gameplay))
				if (controller.value("human", false))
				{
					OperatingCommand command{
					    observation_->own.id, kind + ".select",
					    row.at(kind == "magazine" ? "stock_revision" : "selection_revision").get<std::int64_t>(),
					    system};
					command.console = observation_->console;
					command.controller = controller.at("id");
					command.generation = controller.at("generation");
					command.text = choice;
					if (submit(command))
						return true;
				}
	return false;
}
bool ConsoleModel::logistics_action(const std::string &action, const std::string &system, const std::string &text)
{
	if (!observation_)
		return false;
	const auto board = logistics();
	if (!board.contains("revision"))
		return false;
	for (const auto &ctrl : controller_rows(observation_->gameplay))
		if (ctrl.value("human", false))
		{
			OperatingCommand c{observation_->own.id, "logistics." + action, board.at("revision").get<std::int64_t>(),
			                   system};
			c.console = observation_->console;
			c.controller = ctrl.at("id");
			c.generation = ctrl.at("generation");
			c.text = text;
			if (const auto session = owner_.lock(); session && session->dispatch_console_command(route_, c))
				return true;
		}
	return false;
}
std::optional<ConsoleDockAction> ConsoleModel::target_docking_control(EntityId target) const
{
	if (!target || !observation_ || target == observation_->own.id)
		return std::nullopt;
	const auto board = docking();
	auto human = [&](const std::string &id)
	{
		for (const auto &controller : controller_rows(observation_->gameplay))
			if (controller.at("id") == id)
				return controller.value("human", false);
		return false;
	};
	auto choice = [&](const std::string &action, const Json &port, const std::string &text, const std::string &label)
	{
		const auto key = port.at("id").get<std::string>();
		return ConsoleDockAction{"contact-dock/" + std::to_string(target) + "/" + action + "/" + key + "/" + text,
		                         localise(label), action, key, text};
	};
	for (const auto &port : board.value("ports", Json::array()))
		if (port.at("target") == target && port.at("phase") != "free" && human(port.at("controller")))
		{
			const auto phase = port.at("phase").get<std::string>();
			if (phase == "separating")
				return std::nullopt;
			return choice(phase == "approach" ? "cancel" : "undock", port, "",
			              phase == "approach" ? "Cancel docking" : "Undock");
		}
	bool speed = false, heading = false;
	for (const auto &controller : controller_rows(observation_->gameplay))
		for (const auto &[key, channel] : controller.at("commands").items())
		{
			speed |= channel.at("action") == "speed_mm_s";
			heading |= channel.at("action") == "heading_mdeg";
		}
	for (const auto &remote : board.value("targets", Json::array()))
		if (remote.at("entity") == target)
			for (const auto &port : board.value("ports", Json::array()))
				if (port.at("phase") == "free" && human(port.at("controller")))
					for (const auto &permitted : remote.value("local_ports", Json::array()))
						if (permitted == port.at("id"))
							return choice(speed && heading ? "assist" : "connect", port,
							              std::to_string(target) + "/" + remote.at("port").get<std::string>(), "Dock");
	return std::nullopt;
}
bool ConsoleModel::target_docking_action(EntityId target, const std::string &action)
{
	const auto control = target_docking_control(target);
	return control && control->id == action && docking_action(control->action, control->port, control->text);
}
bool ConsoleModel::docking_action(const std::string &action, const std::string &system, const std::string &text)
{
	if (!observation_)
		return false;
	const auto board = docking();
	if (!board.contains("revision"))
		return false;
	for (const auto &ctrl : controller_rows(observation_->gameplay))
		if (ctrl.value("human", false))
		{
			OperatingCommand c{observation_->own.id, "dock." + action, board.at("revision").get<std::int64_t>(),
			                   system};
			c.console = observation_->console;
			c.controller = ctrl.at("id");
			c.generation = ctrl.at("generation");
			c.text = text;
			if (const auto session = owner_.lock(); session && session->dispatch_console_command(route_, c))
				return true;
		}
	return false;
}
Json ConsoleModel::craft() const
{
	return observation_ ? observation_->gameplay.value("craft", Json::array()) : Json::array();
}
bool ConsoleModel::craft_action(const std::string &action, const std::string &craft_id, const std::string &target)
{
	if (!observation_)
		return false;
	for (const auto &c : craft())
		if (c.at("id") == craft_id && c.value("manual", true))
			for (const auto &ctrl : controller_rows(observation_->gameplay))
				if (ctrl.value("human", false) || ctrl.value("craft_authority", false))
				{
					OperatingCommand command{observation_->own.id, "craft." + action,
					                         c.at("revision").get<std::int64_t>(), craft_id};
					command.console = observation_->console;
					command.controller = ctrl.at("id");
					command.generation = ctrl.at("generation");
					command.text = target;
					const auto session = owner_.lock();
					if (session && session->dispatch_console_command(route_, command))
						return true;
				}
	return false;
}
std::vector<std::pair<std::string, std::string>> ConsoleModel::asset_away_controls(const std::string &asset) const
{
	std::vector<std::pair<std::string, std::string>> controls;
	if (!asset.starts_with("officer:") && !asset.starts_with("team:"))
		return controls;
	for (const auto &mission : away_missions())
	{
		if ((mission.at("phase") != "open" && mission.at("phase") != "recovered") || mission.value("result", "") != "")
			continue;
		const auto key = mission.at("id").get<std::string>();
		for (const auto &slot : mission.at("slots"))
		{
			const auto slot_id = slot.at("id").get<std::string>();
			const auto suffix = key + "/" + slot_id + "/" + asset;
			const auto label = localise(mission.at("title")) + " (" + localise(slot_id) + ")";
			if (slot.at("assigned") == asset && slot.value("owned", false))
				controls.emplace_back("asset-away/withdraw/" + suffix, localise("Withdraw from") + " " + label);
			if (slot.at("assigned") == "")
				for (const auto &choice : slot.at("choices"))
					if (choice == asset)
						controls.emplace_back("asset-away/assign/" + suffix, localise("Send on") + " " + label);
		}
	}
	return controls;
}
bool ConsoleModel::asset_away_action(const std::string &action)
{
	if (!action.starts_with("asset-away/"))
		return false;
	const auto verb_end = action.find('/', 11), mission_end = action.find('/', verb_end + 1),
	           slot_end = action.find('/', mission_end + 1);
	if (verb_end == std::string::npos || mission_end == std::string::npos || slot_end == std::string::npos)
		return false;
	const auto asset = action.substr(slot_end + 1);
	const auto controls = asset_away_controls(asset);
	if (std::none_of(controls.begin(), controls.end(),
	                 [&](const auto &option)
	                 {
		                 return option.first == action;
	                 }))
		return false;
	const auto verb = action.substr(11, verb_end - 11),
	           mission = action.substr(verb_end + 1, mission_end - verb_end - 1),
	           slot = action.substr(mission_end + 1, slot_end - mission_end - 1);
	return away_action(verb, mission, verb == "assign" ? slot + "/" + asset : slot);
}
bool ConsoleModel::away_action(const std::string &action, const std::string &mission, const std::string &text)
{
	if (!observation_)
		return false;
	for (const auto &m : away_missions())
		if (m.at("id") == mission)
			for (const auto &ctrl : controller_rows(observation_->gameplay))
				if (ctrl.value("human", false) ||
				    ((action == "transport" || action == "escort") && ctrl.value("transport_authority", false)) ||
				    ((action == "assign" || action == "withdraw" || action == "contribute") &&
				     ctrl.value("contribution_authority", false)))
				{
					OperatingCommand c{observation_->own.id, "away." + action, m.at("revision").get<std::int64_t>(),
					                   mission};
					c.text = text;
					c.console = observation_->console;
					c.controller = ctrl.at("id");
					c.generation = ctrl.at("generation");
					const auto session = owner_.lock();
					if (session && session->dispatch_console_command(route_, c))
						return true;
				}
	return false;
}
bool ConsoleModel::team_action(const std::string &action, const std::string &team, const std::string &text)
{
	if (!observation_)
		return false;
	for (const auto &t : observation_->gameplay.value("teams", Json::array()))
		if (t.at("id") == team && !t.at("reserved").get<bool>())
			for (const auto &ctrl : controller_rows(observation_->gameplay))
				if (ctrl.contains("widget")
				        ? console_operation_human(ctrl,
				                                  action == "cancel" || text.starts_with("auto/")
				                                      ? std::string("team_assignment")
				                                      : t.value("work_operation", std::string("team_work")),
				                                  team)
				        : ctrl.value("human", false) || t.value("assignment_authority", false))
				{
					OperatingCommand command{observation_->own.id, "team." + action, t.at("revision"), team};
					command.text = text;
					command.console = observation_->console;
					command.controller = ctrl.at("id");
					command.generation = ctrl.at("generation");
					if (submit(command))
						return true;
				}
	return false;
}
Json ConsoleModel::team_map() const
{
	if (!observation_)
		return Json::array();
	const auto teams = observation_->gameplay.value("teams", Json::array());
	const auto controls = observation_->gameplay.value("controls", Json::array());
	if (selected_team_.empty() && !teams.empty() &&
	    std::none_of(teams.begin(), teams.end(),
	                 [](const auto &team)
	                 {
		                 return team.value("manual", true);
	                 }) &&
	    std::none_of(controls.begin(), controls.end(),
	                 [](const auto &c)
	                 {
		                 return c.value("action", "") == "repair_compartment";
	                 }))
		return Json::array();
	auto compartments = observation_->gameplay.value("combat", Json::object()).value("compartments", Json::array());
	for (auto &c : compartments)
	{
		c["dispatch"] = !selected_team_.empty();
		c["eligible"] = selected_team_.empty();
		c["marker"] = "";
	}
	for (const auto &team : observation_->gameplay.value("teams", Json::array()))
		if (team.at("id") == selected_team_)
			for (const auto &order : team.at("orders"))
				if (order.at("id") == selected_team_order_)
					for (auto &c : compartments)
					{
						const bool eligible = std::find(order.at("targets").begin(), order.at("targets").end(),
						                                c.at("index")) != order.at("targets").end();
						c["eligible"] = eligible;
						if (eligible)
							c["marker"] = order.at("target") == "patients" ? "+ " + localise("Medical")
							              : order.at("target") == "damage" ? "! " + localise("Damage")
							                                               : order.at("icon").get<std::string>();
					}
	return compartments;
}
Json ConsoleModel::personnel() const
{
	return observation_ ? observation_->gameplay.value("personnel", Json::object()) : Json::object();
}
bool ConsoleModel::personnel_action(const std::string &action, const std::string &officer, const std::string &duty)
{
	if (!observation_ || personnel().empty())
		return false;
	OperatingCommand command{observation_->own.id, "personnel." + action, 0, officer};
	command.console = observation_->console;
	command.text = duty;
	command.generation = personnel().at("revision");
	const auto session = owner_.lock();
	return session && session->dispatch_console_command(route_, command);
}
Json ConsoleModel::conversations() const
{
	return observation_ ? observation_->gameplay.value("conversations", Json::object()) : Json::object();
}
bool ConsoleModel::conversation_action(const std::string &action, std::uint64_t conversation, const std::string &text,
                                       std::uint64_t prompt)
{
	if (!observation_)
		return false;
	for (const auto &section : controller_rows(observation_->gameplay))
	{
		if (!section.value("human", false) && !section.value("conversation_authority", false))
			continue;
		bool owns = false;
		for (const auto &command : section.at("commands"))
			for (const auto &consumer : command.at("consumers"))
				for (const auto &system : observation_->systems)
					if (system.kind == OperatingSystem::Comms && system.id == consumer.get<std::string>())
						owns = true;
		if (!owns)
			continue;
		OperatingCommand command{observation_->own.id, "comms." + action, static_cast<std::int64_t>(conversation)};
		command.console = observation_->console;
		command.controller = section.at("id");
		command.generation = section.at("generation");
		command.request_id = prompt;
		command.text = text;
		auto session = owner_.lock();
		const auto eligible =
		    session ? eligible_console_command(*observation_, layout_, session->public_board(), command) : std::nullopt;
		return eligible && session->dispatch_console_command(route_, eligible->command);
	}
	return false;
}
bool ConsoleModel::select_contact(EntityId contact)
{
	if (!observation_ || (!std::any_of(observation_->contacts.begin(), observation_->contacts.end(),
	                                   [&](const auto &row)
	                                   {
		                                   return row.id == contact;
	                                   }) &&
	                      !std::any_of(observation_->bearing_contacts.begin(), observation_->bearing_contacts.end(),
	                                   [&](const auto &row)
	                                   {
		                                   return row.id == contact;
	                                   })))
		return false;
	combat_target_ = sensor_contact_ = static_cast<std::int64_t>(contact);
	for (const auto &descriptor : observation_->gameplay.value("controls", Json::array()))
		if (descriptor.value("shared_combat_target", false) &&
		    descriptor.value("value", std::int64_t{}) == static_cast<std::int64_t>(contact))
			return submit(decode_operating_command(descriptor));
	return true;
}
} // namespace phoenix
