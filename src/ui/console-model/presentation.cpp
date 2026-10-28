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
const Json *ConsoleModel::controller_section(const std::string &binding) const
{
	if (!observation_ || !binding.starts_with("controller/"))
		return nullptr;
	const auto split = binding.find('/', 11);
	if (split == std::string::npos || !observation_->gameplay.contains("controllers"))
		return nullptr;
	const Json *found = nullptr;
	for (const auto &controller : observation_->gameplay.at("controllers"))
		if (controller.value("id", std::string{}) == binding.substr(11, split - 11))
		{
			if (found)
				return nullptr;
			found = &controller;
		}
	return found;
}
const Json *ConsoleModel::controller_channel(const std::string &binding) const
{
	const auto owner = controller_section(binding);
	if (!owner || !owner->contains("commands"))
		return nullptr;
	const auto channel = binding.substr(binding.find('/', 11) + 1);
	const auto &commands = owner->at("commands");
	return commands.contains(channel) ? &commands.at(channel) : nullptr;
}
std::vector<ConsoleSystemStatus> ConsoleModel::system_statuses() const
{
	std::vector<ConsoleSystemStatus> result;
	if (!observation_ || !observation_->gameplay.is_object() ||
	    borrowed_member(observation_->gameplay, "combat").empty())
		return result;
	for (const auto &system : observation_->systems)
	{
		if (system.kind == OperatingSystem::Team)
			continue; // Team activity/condition is presented by the roster.
		const auto flag = [&](const char *key)
		{
			return system.telemetry.contains(key) && system.telemetry.at(key) != 0;
		};
		const auto condition = flag("destroyed")             ? SystemCondition::Destroyed
		                       : flag("overheated")          ? SystemCondition::Overheated
		                       : flag("structural_disabled") ? SystemCondition::CompartmentDisabled
		                       : flag("damaged")             ? SystemCondition::Damaged
		                       : system.enabled              ? SystemCondition::Operational
		                                                     : SystemCondition::Off;
		const auto label = condition == SystemCondition::Destroyed             ? "Destroyed"
		                   : condition == SystemCondition::Overheated          ? "Overheated"
		                   : condition == SystemCondition::CompartmentDisabled ? "Compartment disabled"
		                   : condition == SystemCondition::Damaged             ? "Damaged"
		                   : condition == SystemCondition::Operational         ? "Operational"
		                                                                       : "Off";
		result.push_back({system.id, system.id + ": " + localise(label),
		                  std::string(condition == SystemCondition::Operational ? "[+] " : "[!] ") + system.id,
		                  condition});
		if (system.telemetry.contains("officer_condition") && system.telemetry.at("officer_condition") > 0)
		{
			const auto medical = system.telemetry.at("officer_condition");
			const auto label = medical == 1   ? "Officer stunned"
			                   : medical == 2 ? "Officer injured"
			                   : medical == 3 ? "Officer severely injured"
			                                  : "Officer killed";
			result.back().semantic_label += "; " + localise(label);
			result.back().compact_label = "+ " + result.back().compact_label;
		}
	}
	return result;
}
std::vector<std::pair<std::string, std::string>> ConsoleModel::gameplay_controls() const
{
	std::vector<std::pair<std::string, std::string>> result;
	if (available_consoles_.size() > 1)
		for (const auto &[key, label] : available_consoles_)
			result.emplace_back("game.console/" + key, localise("Open console") + " " + localise(label));
	if (!observation_ || !observation_->gameplay.is_object())
		return result;
	const auto &sensors = borrowed_member(observation_->gameplay, "sensors");
	for (const auto &contact : observation_->contacts)
		result.emplace_back("game.sensor-contact/" + std::to_string(contact.id), localise("Inspect contact") + " " +
		                                                                             localise(contact.definition) +
		                                                                             " " + std::to_string(contact.id));
	for (const auto &b : observation_->bearing_contacts)
		result.emplace_back("game.sensor-contact/" + std::to_string(b.id), localise("Inspect contact") + " " +
		                                                                       std::to_string(b.id) + "; " +
		                                                                       bearing_readout(b.bearing_mdeg));
	for (const auto &contact : borrowed_member(sensors, "contacts", true))
		if (std::none_of(observation_->contacts.begin(), observation_->contacts.end(),
		                 [&](const auto &c)
		                 {
			                 return c.id == contact.at("id").get<EntityId>();
		                 }) &&
		    std::none_of(observation_->bearing_contacts.begin(), observation_->bearing_contacts.end(),
		                 [&](const auto &b)
		                 {
			                 return b.id == contact.at("id").get<EntityId>();
		                 }))
			result.emplace_back("game.sensor-contact/" + std::to_string(contact.at("id").get<EntityId>()),
			                    localise("Last observed") + " " + std::to_string(contact.at("id").get<EntityId>()));
	if (!sensors.empty())
		result.emplace_back("game.contact-arcs",
		                    localise("Contact fire arcs") + ": " + localise(contact_arcs_ ? "On" : "Off"));
	const auto &combat = borrowed_member(observation_->gameplay, "combat");
	for (const auto &status : system_statuses())
		result.emplace_back("game.status/" + status.system, status.semantic_label);
	if (!status_system_.empty())
		result.emplace_back("game.status-close", localise("Close system details"));
	bool weapons = false;
	for (const auto &descriptor : borrowed_member(observation_->gameplay, "controls", true))
		weapons |= descriptor.value("action", std::string{}) == "weapon_target" ||
		           descriptor.value("action", std::string{}) == "directed_target" ||
		           descriptor.value("shared_combat_target", false);
	if (weapons)
		for (const auto &contact : observation_->contacts)
			if (contact.radar_icon == RadarIcon::ship)
				result.emplace_back(
				    "game.combat-target/" + std::to_string(contact.id),
				    localise("Select target") + " " + contact.definition + " " + std::to_string(contact.id) + " — " +
				        localise(contact.faction_label.empty() ? "Unaffiliated" : contact.faction_label) + ", " +
				        localise(contact.disposition) +
				        (combat_target_ == static_cast<std::int64_t>(contact.id) ? " [*]" : ""));
	if (weapons)
		for (const auto &b : observation_->bearing_contacts)
			result.emplace_back("game.combat-target/" + std::to_string(b.id),
			                    localise("Select target") + " " + std::to_string(b.id) + "; " +
			                        localise("Range unknown") +
			                        (combat_target_ == static_cast<std::int64_t>(b.id) ? " [*]" : ""));
	if (!selected_team_.empty())
		result.emplace_back("game.teams-close", localise("Close team dispatch"));
	for (const auto &team : borrowed_member(observation_->gameplay, "teams", true))
	{
		if (team.at("reserved").get<bool>() || !team.value("manual", true))
			continue;
		for (const auto &order : team.at("orders"))
			result.emplace_back(
			    "game.team-order/" + team.at("id").get<std::string>() + "/" + order.at("id").get<std::string>(),
			    team.at("id").get<std::string>() + " (" + localise(team.value("condition", "healthy")) +
			        "): " + localise(order.at("label")) +
			        (team.at("id") == selected_team_ && order.at("id") == selected_team_order_ ? " [*]" : ""));
		if (team.at("order") != "" || team.value("assigned_compartment", -1) >= 0)
			result.emplace_back("game.team-cancel/" + team.at("id").get<std::string>(),
			                    localise("Cancel team order") + " " + team.at("id").get<std::string>());
		if (team.at("id") == selected_team_ && team.value("patient_control", true) && selected_team_order_ != "auto")
			for (const auto &p : team.at("patients"))
				if (p.at("condition") == "minor" || p.at("condition") == "severe")
					result.emplace_back("game.team-patient/" + p.at("id").get<std::string>(),
					                    localise("Prioritise patient") + " " + p.at("name").get<std::string>() + ": " +
					                        localise(p.at("condition")));
	}
	for (const auto &c : team_map())
		result.emplace_back("game.compartment/" + std::to_string(c.at("index").get<std::int64_t>()),
		                    c.value("marker", std::string{}) + " " + c.at("id").get<std::string>() + " " +
		                        std::to_string(c.at("hp").get<std::int64_t>()) + "/" +
		                        std::to_string(c.at("maximum").get<std::int64_t>()) +
		                        (selected_compartment_ == c.at("index").get<std::int64_t>() ? " [*]" : ""));
	if (layout_.is_object())
		for (const auto &w : layout_.at("widgets"))
			if (w.value("fire_arcs", false))
			{
				result.emplace_back("game.fire-arcs",
				                    localise("Fire arcs") + ": " + localise(fire_arcs_ ? "On" : "Off"));
				break;
			}
	const auto &controls = borrowed_member(observation_->gameplay, "controls", true);
	if (!controls.is_array())
		return result;
	std::map<std::string, std::size_t> occurrences;
	for (const auto &descriptor : controls)
		if (console_descriptor(descriptor))
			++occurrences[console_string(descriptor, "id")];
	for (const auto &descriptor : controls)
	{
		if (!console_descriptor(descriptor))
			continue;
		if (!borrowed_member(observation_->gameplay, "ftl", true).empty() &&
		    descriptor.value("action", "") != "system_enabled" && descriptor.value("action", "") != "ftl.depart")
			continue;
		if (descriptor.value("action", std::string{}) == "system_enabled")
		{
			bool team_switch = false;
			for (const auto &controller : controller_rows(observation_->gameplay))
				if (controller.at("id") == descriptor.value("controller", std::string{}))
					for (const auto &[channel, command] : controller.at("commands").items())
						if (channel == descriptor.value("channel", std::string{}))
							for (const auto &consumer : command.at("consumers"))
								for (const auto &system : observation_->systems)
									team_switch |= system.id == consumer.get<std::string>() &&
									               system.kind == OperatingSystem::Team;
			if (team_switch)
				continue; // Teams have activities and conditions, not player-operated readiness switches.
			bool combat_control = false;
			for (const auto &system : observation_->systems)
				if (system.id == descriptor.value("controller", std::string{}) &&
				    system.kind >= OperatingSystem::Weapon && system.kind < OperatingSystem::Shield)
					combat_control = true;
			if (combat_control)
				continue;
		}

		// Keep neutral in the admitted projection for remote clients; the two
		// visible direction buttons provide its press/release interaction.
		if (descriptor.value("action", "") == "lateral_thrust" && descriptor.value("value", std::int64_t{0}) == 0)
			continue;
		if (descriptor.value("action", std::string{}) == "repair_compartment" ||
		    descriptor.value("action", std::string{}) == "repair_system")
		{
			bool authored = false;
			for (const auto &controller : controller_rows(observation_->gameplay))
				if (controller.at("id") == descriptor.value("controller", std::string{}))
					for (const auto &[key, channel] : controller.at("commands").items())
						if (key == descriptor.value("channel", std::string{}))
							for (const auto &consumer : channel.at("consumers"))
								for (const auto &team : borrowed_member(observation_->gameplay, "teams", true))
									if (team.at("id") == consumer && !team.at("orders").empty())
										authored = true;
			if (authored)
				continue;
		}
		if (descriptor.contains("combat_choice"))
		{
			const auto value = descriptor.at("value").get<std::int64_t>();
			const auto selected = descriptor.at("combat_choice") == "weapon"   ? combat_target_
			                      : descriptor.at("combat_choice") == "sensor" ? sensor_contact_
			                                                                   : selected_compartment_;
			if (value && value != selected)
				continue;
		}
		const auto id = console_string(descriptor, "id");
		if (occurrences[id] != 1)
			continue;
		auto label = console_label(console_string(descriptor, "label_key"));
		if (label.empty())
			label = console_label(console_string(descriptor, "label"));
		if (descriptor.value("label_value", Json(false)) == true)
		{
			const auto value = *console_integer(descriptor.at("value"));
			if (descriptor.value("action", "") == "system_enabled")
				label = localise(value ? "Enable" : "Disable") + " " + label;
			else
				label += " " + std::to_string(value);
		}
		label =
		    console_execution_prefix(descriptor) + localise(descriptor.value("label_prefix", std::string{})) + label;
		if (descriptor.value("selected", false))
			label += " (" + localise("Selected") + ")";
		if (label.empty())
			continue;
		result.emplace_back("game.control/" + console_encoded(id), std::move(label));
	}
	std::stable_partition(result.begin(), result.end(),
	                      [](const auto &control)
	                      {
		                      return !control.first.starts_with("game.status/");
	                      });
	return result;
}
std::string ConsoleModel::system_details() const
{
	if (!observation_ || status_system_.empty())
		return {};
	std::string text = status_system_;
	bool fault = false;
	const auto &combat = borrowed_member(observation_->gameplay, "combat");
	for (const auto &f : borrowed_member(combat, "faults", true))
		if (f.at("system") == status_system_)
		{
			fault = true;
			text += ". " + localise(f.at("symptom"));
			text += ". " + localise(f.at("diagnosed").get<bool>() ? f.at("diagnosis").get<std::string>()
			                                                      : "Awaiting team inspection");
			text += ". " + localise(f.at("destroyed").get<bool>() ? "Destroyed: cannot repair during this encounter"
			                                                      : "Repairable");
			if (f.at("diagnosed").get<bool>() && !f.at("destroyed").get<bool>())
				text += " " + std::to_string(f.at("work").get<std::int64_t>()) + "/" +
				        std::to_string(f.at("required_work").get<std::int64_t>());
		}
	for (const auto &system : observation_->systems)
		if (system.id == status_system_)
		{
			const auto flag = [&](const char *key)
			{
				return system.telemetry.contains(key) && system.telemetry.at(key) != 0;
			};
			if (flag("overheated"))
				text += ". " + localise("Overheated: waiting for cooling below recovery threshold");
			if (flag("structural_disabled"))
				text += ". " + localise("Disabled: compartment structure");
			if (!fault && !flag("overheated") && !flag("structural_disabled"))
				text += ". " + localise("No known faults");
		}
	return text;
}
std::vector<std::pair<std::string, std::string>> ConsoleModel::gameplay_readouts() const
{
	std::vector<std::pair<std::string, std::string>> result;
	if (!observation_)
		return result;
	if (observation_->gameplay.contains("objective_board"))
	{
		const auto &board = observation_->gameplay.at("objective_board");
		if (board.at("objectives").empty())
			result.emplace_back("game.objective/empty", localise("No published objectives"));
		for (const auto &objective : board.at("objectives"))
		{
			std::string text = objective.value("priority", false) ? localise("Priority objective") + ": " : "";
			text += localise(objective.at("title")) + " — " + console_objective_status(objective.at("status"));
			if (!objective.value("description", std::string{}).empty())
				text += ". " + localise(objective.at("description"));
			if (objective.contains("destination_x") && objective.contains("destination_y"))
				text += ". " + localise("Destination") + ": " +
				        std::to_string(objective.at("destination_x").get<std::int64_t>()) + ", " +
				        std::to_string(objective.at("destination_y").get<std::int64_t>());
			result.emplace_back("game.objective/" + std::to_string(objective.at("id").get<std::uint64_t>()),
			                    std::move(text));
		}
		if (!board.at("can_select").get<bool>())
			result.emplace_back("game.objective/authority",
			                    localise("Priority selection requires an AI-operated Controller"));
	}
	for (const auto &ftl : borrowed_member(observation_->gameplay, "ftl", true))
	{
		const auto capacity = ftl.at("capacity").get<std::int64_t>();
		result.emplace_back(
		    "game.readout/ftl",
		    localise("FTL battery") + ": " +
		        std::to_string(ftl.at("energy").get<std::int64_t>() * 100 / std::max<std::int64_t>(1, capacity)) +
		        "% — " +
		        localise(ftl.at("ready").get<bool>() ? "Ready to depart" : ftl.at("blocker").get<std::string>()) +
		        ". " + localise("Power draw") + ": " + std::to_string(ftl.at("power_draw").get<std::int64_t>()) + ". " +
		        localise("Heat") + ": " + std::to_string(ftl.at("heat").get<std::int64_t>()));
		if (ftl.value("visitors", 0) > 0)
			result.emplace_back("game.readout/ftl-visitors", localise("Visiting personnel and transfers") + ": " +
			                                                     std::to_string(ftl.at("visitors").get<int>()));
		for (const auto &name : ftl.at("left_behind"))
			result.emplace_back("game.readout/ftl-away/" + name.get<std::string>(),
			                    localise("Personnel left behind") + ": " + localise(name));
	}
	if (!status_system_.empty())
		result.emplace_back("game.readout/system-details", system_details());
	for (const auto &directed : borrowed_member(observation_->gameplay, "directed", true))
	{
		std::string text = directed.at("id").get<std::string>();
		for (const auto *selection : {"operating_mode", "targeting_mode"})
		{
			const auto choices = directed.value(std::string(selection) + "s", Json::array());
			for (const auto &choice : choices)
				if (choice.at("value") == directed.at(selection))
					text += "; " +
					        localise(selection == std::string("operating_mode") ? "Operating mode" : "Targeting mode") +
					        ": " + localise(choice.at("label"));
		}
		text += "; " + localise(directed.value("ready", false) ? "Ready"
		                                                       : directed.value("blocker", std::string("Not ready")));
		if (directed.value("target", EntityId{0}))
			text += "; " + localise("Target") + " " + std::to_string(directed.at("target").get<EntityId>());
		result.emplace_back("game.directed/" + directed.at("id").get<std::string>(), std::move(text));
	}
	for (const auto &rule : borrowed_member(observation_->gameplay, "rules", true))
	{
		auto text = localise(rule.at("label")) + "; " + localise(rule.at("phase")) + "; " + localise("Rule output") +
		            " " + std::to_string(rule.at("output").get<std::int64_t>() / 10) + "%";
		static const std::map<std::string, std::string> blockers{
		    {"conditions", "Rule conditions unmet"}, {"source", "Rule source unavailable"},
		    {"funding", "Rule funding unavailable"}, {"expression", "Rule value unavailable"},
		    {"target", "Rule target unavailable"},   {"stopped", "Rule stopped"}};
		if (const auto blocker = blockers.find(rule.at("blocker").get<std::string>()); blocker != blockers.end())
			text += "; " + localise(blocker->second);
		result.emplace_back("game.rule/" + console_encoded(rule.at("id").get<std::string>()), std::move(text));
	}
	for (const auto &ability : borrowed_member(observation_->gameplay, "abilities", true))
	{
		auto text = localise(ability.at("label")) + "; " + localise(ability.at("phase"));
		const auto preparation = ability.at("preparation_ticks").get<std::int64_t>();
		if (preparation && (ability.at("phase") == "preparing" || ability.at("phase") == "paused"))
			text += "; " + localise("Preparation") + " " +
			        std::to_string(ability.at("progress").get<std::int64_t>() * 100 / (preparation * 1000)) + "%";
		if (ability.at("ready").get<std::uint64_t>() > observation_->observed_tick)
			text +=
			    "; " + localise("Recovery until tick") + " " + std::to_string(ability.at("ready").get<std::uint64_t>());
		if (!ability.at("blocker").get<std::string>().empty())
			text += "; " + localise(ability.at("blocker"));
		text += "; " + localise("Energy per second") + " " +
		        std::to_string(ability.at("energy_per_second").get<std::int64_t>());
		text += console_property_readout(ability.value("properties", Json::array()));
		result.emplace_back("game.ability/" + ability.at("id").get<std::string>(), std::move(text));
	}
	for (const auto &process : borrowed_member(observation_->gameplay, "processes", true))
	{
		auto text = localise(process.at("label")) + "; " +
		            localise(process.at("driver").get<bool>() ? Json("Automatic work") : process.at("phase"));
		text += "; " + localise(process.at("requested").get<bool>() ? "Requested" : "Stopped");
		if (!process.at("finite").get<bool>() || process.at("details_available").get<bool>())
		{
			text += "; " + localise("Funded activity") + " " +
			        std::to_string(process.at("output").get<std::int64_t>() / 10) + "%";
			std::ostringstream work;
			work << std::fixed << std::setprecision(3)
			     << static_cast<double>(process.at("work").get<std::int64_t>()) /
			            process.value("display_scale", std::int64_t{1});
			auto amount = work.str();
			while (amount.ends_with('0'))
				amount.pop_back();
			if (amount.ends_with('.'))
				amount.pop_back();
			text += "; " + localise("Completed work") + " " + amount + " " + localise(process.at("work_unit"));
		}
		if (process.at("finite").get<bool>() && process.at("details_available").get<bool>())
		{
			if (!process.at("target").get<std::string>().empty())
				text += "; " + localise("Target") + " " + process.at("target").get<std::string>();
			const auto required = process.at("required_work").get<std::int64_t>();
			if (required > 0)
				text += "; " + localise("Job progress") + " " +
				        std::to_string(
				            std::min<std::int64_t>(100, process.at("progress").get<std::int64_t>() * 100 / required)) +
				        "%";
			if (process.at("completed").get<bool>())
				text += "; " + localise("Job completed");
			if (process.at("ready").get<bool>())
				text += "; " + localise("Ready");
		}

		if (!process.at("blocker").get<std::string>().empty())
			text += "; " + localise(process.at("blocker"));
		text += console_property_readout(process.value("properties", Json::array()));
		result.emplace_back("game.process/" + process.at("id").get<std::string>(), std::move(text));
	}
	const auto &sensors = borrowed_member(observation_->gameplay, "sensors");
	for (const auto &b : observation_->bearing_contacts)
		if (b.id == static_cast<EntityId>(sensor_contact_))
			result.emplace_back("game.scan-bearing/" + std::to_string(b.id), localise("Contact") + " " +
			                                                                     std::to_string(b.id) + "; " +
			                                                                     bearing_readout(b.bearing_mdeg));
	for (const auto &scanner : borrowed_member(sensors, "scanners", true))
	{
		const auto key = scanner.at("id").get<std::string>();
		if (scanner.at("target") != 0)
			result.emplace_back("game.scan-status/" + key, localise("Scanning") + " " +
			                                                   std::to_string(scanner.at("target").get<EntityId>()) +
			                                                   " / " + localise(scanner.at("blocker")));
		for (const auto &progress : scanner.at("progress"))
			result.emplace_back("game.scan-progress/" + key + "/" + progress.at("capability").get<std::string>(),
			                    localise(progress.at("capability")) + ": " +
			                        std::to_string(progress.at("ticks").get<std::int64_t>() * 100 /
			                                       std::max<std::int64_t>(1, progress.at("required"))) +
			                        "%");
	}
	for (const auto &contact : borrowed_member(sensors, "contacts", true))
		if (contact.at("id") == sensor_contact_)
		{
			for (const auto &[cap, finding] : contact.at("findings").items())
			{
				const auto key = "game.scan-finding/" + std::to_string(sensor_contact_) + "/" + cap;
				result.emplace_back(key, localise(cap.starts_with("finding:") ? "Sensor finding" : cap) + " / " +
				                             localise(finding.at("live").get<bool>() ? "Live" : "Last observed") + " " +
				                             std::to_string(finding.at("observed").get<std::uint64_t>() / 20) + "s");
				const auto &data = finding.at("data");
				if (cap == "basic_identity")
					result.emplace_back(key + "/identity", localise(data.at("class")) + " / " +
					                                           localise(data.at("faction_label")) + " / " +
					                                           localise(data.at("disposition")));
				else if (cap.starts_with("finding:"))
					result.emplace_back(key + "/text", localise(data.at("text")));
				else if (cap == "findings")
					for (const auto &r : data)
						result.emplace_back(key + "/" + r.at("id").get<std::string>(), localise(r.at("text")));
				else if (cap == "shield_sectors" || cap == "compartment_condition")
					for (const auto &r : data)
						result.emplace_back(
						    key + "/" + r.at("id").get<std::string>(),
						    r.at("id").get<std::string>() + ": " +
						        std::to_string(r.at(cap == "shield_sectors" ? "strength" : "hp").get<std::int64_t>()) +
						        "/" +
						        std::to_string(
						            r.at(cap == "shield_sectors" ? "capacity" : "maximum").get<std::int64_t>()));
			}
		}
	const auto &combat = borrowed_member(observation_->gameplay, "combat");
	if (!combat.empty())
	{
		if (combat.contains("result"))
			result.emplace_back("combat.outcome", localise(combat.at("result")));
		if (combat.value("destroyed", false))
			result.emplace_back("combat.result", localise("Ship destroyed"));
		for (const auto &c : borrowed_member(combat, "compartments", true))
			result.emplace_back("compartment." + c.at("id").get<std::string>(),
			                    c.at("deck").get<std::string>() + " / " + c.at("id").get<std::string>() + ": " +
			                        std::to_string(c.at("hp").get<std::int64_t>()) + "/" +
			                        std::to_string(c.at("maximum").get<std::int64_t>()) + " " +
			                        localise(c.at("disabled").get<bool>() ? "Disabled" : "Operational"));
		for (const auto &t : borrowed_member(combat, "teams", true))
			result.emplace_back(
			    "team." + t.at("id").get<std::string>(),
			    t.at("id").get<std::string>() + ": " + localise("Compartment") + " " +
			        std::to_string(t.at("location").get<std::int64_t>() + 1) + "; " + localise("Destination") + " " +
			        std::to_string(t.at("destination").get<std::int64_t>() + 1) + "; " + localise("Travel remaining") +
			        " " + std::to_string(t.at("travel").get<std::int64_t>()));
		if (fire_arcs_ && layout_.is_object() &&
		    std::any_of(layout_.at("widgets").begin(), layout_.at("widgets").end(),
		                [](const auto &w)
		                {
			                return w.value("fire_arcs", false);
		                }))
			for (const auto &arc : borrowed_member(combat, "arcs", true))
				result.emplace_back("arc." + arc.at("id").get<std::string>(),
				                    arc.at("id").get<std::string>() + ": " + localise("Fire arc") + " " +
				                        std::to_string(arc.at("arc_mdeg").get<std::int64_t>() / 1000) + "; " +
				                        localise("Bearing") + " " +
				                        std::to_string(arc.at("bearing_mdeg").get<std::int64_t>() / 1000));
	}
	for (const auto &shield : borrowed_member(combat, "shields", true))
		result.emplace_back("shield." + shield.value("component_id", shield.at("id").get<std::string>()),
		                    shield.at("id").get<std::string>() + ": " + localise("Stored strength") + " " +
		                        std::to_string(shield.at("strength").get<std::int64_t>()) + "/" +
		                        std::to_string(shield.at("capacity").get<std::int64_t>()) + "; " + localise("Warm-up") +
		                        " " + std::to_string(shield.at("warmup").get<std::int64_t>()) + "/" +
		                        std::to_string(shield.at("warmup_ticks").get<std::int64_t>()));
	for (const auto &weapon : borrowed_member(combat, "weapons", true))
	{
		std::string text = weapon.at("id").get<std::string>();
		if (!weapon.at("magazines").empty())
			text +=
			    "; " + localise("Selected magazine") + " " +
			    weapon.at("magazines").at(weapon.at("selected").get<std::size_t>()).get<std::string>() + "; " +
			    localise(weapon.at("unloading").get<std::int64_t>() > 0 ? "Unloading" : "Loading") + " " +
			    std::to_string(weapon.at("unloading").get<std::int64_t>() > 0
			                       ? weapon.at("unloading").get<std::int64_t>()
			                       : weapon.at("loading").get<std::int64_t>()) +
			    "/" +
			    std::to_string(weapon.at(weapon.at("unloading").get<std::int64_t>() > 0 ? "unload_ticks" : "load_ticks")
			                       .get<std::int64_t>()) +
			    "; " + localise(weapon.at("loaded").get<std::int64_t>() < 0 ? "Unloaded" : "Loaded");
		if (weapon.at("point_defence").get<bool>())
			text += "; " + localise(weapon.at("mode") == 0 ? "Off" : weapon.at("mode") == 1 ? "Auto" : "Manual");
		result.emplace_back("weapon." + weapon.at("id").get<std::string>(), text);
	}
	for (const auto *key : {"alert", "stance"})
	{
		const auto state = observation_->gameplay.value(key, Json::object());
		if (!state.empty())
			result.emplace_back(key, localise(key == std::string("alert") ? "Alert condition" : "Stance") + ": " +
			                             localise(state.at("label")));
	}
	const auto session = owner_.lock();
	const auto now = session ? session->ticks() : 0;
	for (const auto &system : observation_->systems)
	{
		if (system.telemetry.empty() && system.status.empty())
			continue;
		if (!combat.empty())
		{
			auto name = system.id;
			std::replace(name.begin(), name.end(), '-', ' ');
			const bool disabled =
			    system.telemetry.contains("structural_disabled") && system.telemetry.at("structural_disabled");
			std::string text = name + (disabled ? ": [!] " : ": [+] ") +
			                   localise(disabled         ? "Disabled: compartment structure"
			                            : system.enabled ? "Operational"
			                                             : "Off");
			if (system.kind == OperatingSystem::Magazine)
				text += "; " + localise("Ammunition") + " " + std::to_string(system.telemetry.at("ammo")) + "/" +
				        std::to_string(system.telemetry.at("capacity")) + "; " + localise("Reload") + " " +
				        std::to_string(system.telemetry.at("reload_progress")) + "/" +
				        std::to_string(system.telemetry.at("reload_ticks"));
			if (system.kind == OperatingSystem::Weapon)
				text +=
				    "; " + localise("Target") + " " + std::to_string(system.telemetry.at("target")) + "; " +
				    localise(system.telemetry.at("ready") ? "Firing solution ready" : "Waiting for firing solution");
			if (system.kind == OperatingSystem::Turret)
				text += "; " + localise("Bearing") + " " + std::to_string(system.telemetry.at("heading_mdeg") / 1000);
			if (system.kind == OperatingSystem::Engine || system.kind == OperatingSystem::Steering)
				text += "; " + localise("Requested") + " " + std::to_string(system.requested);
			result.emplace_back("game.readout/system-" + console_encoded(system.id), std::move(text));
			continue;
		}
		std::string text = system.id + ": " + console_label(system.status);
		if (system.telemetry.contains("structural_disabled"))
			text += (system.telemetry.at("structural_disabled") ? " [!] " : " [+] ") +
			        localise(system.telemetry.at("structural_disabled") ? "Disabled: compartment structure"
			                                                            : "Operational");
		if (system.telemetry.contains("ammo") && system.kind == OperatingSystem::Magazine)
			text += "; " + localise("Ammunition") + " " + std::to_string(system.telemetry.at("ammo")) + "/" +
			        std::to_string(system.telemetry.at("capacity"));
		text += "; " + localise("Requested") + " " + std::to_string(system.requested);
		if (system.kind == OperatingSystem::Impulse && system.telemetry.contains("impulse_phase"))
		{
			const auto &t = system.telemetry;
			const auto phase = t.at("impulse_phase");
			text += "; " + localise(phase == 1 ? "Charging" : phase == 2 ? "Transit" : "Idle");
			text += "; " + localise("Charge progress") + " " + std::to_string(t.at("impulse_progress")) + "/" +
			        std::to_string(t.at("impulse_charge_total"));
			text += "; " + localise("Charge energy spent") + " " + std::to_string(t.at("impulse_charge_spent"));
			static const std::array<const char *, 7> reasons{"",
			                                                 "Impulse battery reserve insufficient",
			                                                 "Impulse charging paused: insufficient energy",
			                                                 "Impulse cancelled: insufficient operating power",
			                                                 "Impulse cancelled: equipment disabled",
			                                                 "Impulse cancelled: collision",
			                                                 "Impulse cancelled manually"};
			const auto reason = t.at("impulse_reason");
			if (reason > 0 && reason < static_cast<std::int64_t>(reasons.size()))
				text += "; " + localise(reasons[reason]);
			if (!phase)
				text +=
				    "; " + localise(t.at("impulse_ready") ? "Impulse ready" : "Impulse battery reserve insufficient");
		}
		for (const auto &[key, value] : system.telemetry)
			if (system.kind < OperatingSystem::Weapon && key != "structural_disabled")
				text += "; " + console_label(key) + " " + std::to_string(value);
		if (system.telemetry.contains("depletion_ticks") || system.telemetry.contains("coolant_depletion_ticks"))
			text +=
			    "; " +
			    localise("Estimates assume current demand and supply remain constant; future commands are excluded.");
		result.emplace_back("game.readout/system-" + console_encoded(system.id), std::move(text));
	}
	for (const auto &c : borrowed_member(observation_->gameplay, "psychic").value("choirs", Json::array()))
	{
		for (const auto &seat : c.at("seats"))
		{
			std::string value = seat.at("officer").get<std::string>() + ": " + localise(seat.at("power"));
			value += ". " + localise("Effective strength") + " " + std::to_string(seat.at("work").get<int>()) + "/1000";
			if (!seat.at("blocker").get<std::string>().empty())
				value += ". " + localise(seat.at("blocker"));
			result.emplace_back("psychic." + seat.at("duty").get<std::string>(), value);
		}
	}

	for (const auto &t : borrowed_member(observation_->gameplay, "sensors").value("tracks", Json::array()))
	{
		const auto &b = t.at("body");
		result.emplace_back("track." + std::to_string(b.at("id").get<EntityId>()),
		                    localise("Last observed") + " " + localise(b.at("definition")) + ". " +
		                        localise("Predicted position") + " " + std::to_string(b.at("x").get<std::int64_t>()) +
		                        ", " + std::to_string(b.at("y").get<std::int64_t>()) + ". " + localise("Uncertainty") +
		                        " " + std::to_string(t.at("uncertainty").get<std::int64_t>()) + ". " +
		                        localise("Observed tick") + " " +
		                        std::to_string(t.at("observed").get<std::uint64_t>()));
	}
	for (auto row : board_readouts(observation_->gameplay, now))
		if (!observation_->gameplay.contains("objective_board"))
			result.push_back(std::move(row));
	const auto marker = waypoint();
	if (marker.value("active", false))
	{
		const double dx = marker.value("x", 0.0) - observation_->own.x,
		             dy = marker.value("y", 0.0) - observation_->own.y;
		result.emplace_back(
		    "game.readout/waypoint-guidance",
		    localise("Waypoint") + ": " + std::to_string(static_cast<int>(std::hypot(dx, dy) / 1000)) + " m; " +
		        std::to_string(static_cast<int>(std::fmod(std::atan2(dy, dx) * 180 / 3.141592653589793 + 360, 360))) +
		        " degrees");
	}
	return result;
}

std::string ConsoleModel::contacts_text() const
{
	if (!observation_ || !(observation_->feeds & system_bit(OperatingSystem::Radar)))
		return {};
	std::ostringstream text;
	text << "Radar: " << (observation_->contacts.size() + observation_->bearing_contacts.size()) << " contacts.";
	for (const auto &r : observation_->regions)
		if (r.value("navigation", false) ||
		    (r.value("radar", false) &&
		     std::any_of(observation_->systems.begin(), observation_->systems.end(),
		                 [&](const auto &sensor)
		                 {
			                 return sensor.kind == OperatingSystem::Radar &&
			                        std::any_of(r.at("tags").begin(), r.at("tags").end(),
			                                    [&](const auto &tag)
			                                    {
				                                    return std::find(sensor.shows.begin(), sensor.shows.end(),
				                                                     tag.template get<std::string>()) !=
				                                           sensor.shows.end();
			                                    });
		                 })))
			text << " " << localise(r.at("name").get<std::string>()) << ", " << r.at("x").get<std::int64_t>() / 1000
			     << ", " << r.at("y").get<std::int64_t>() / 1000 << " m.";
	for (const auto &sensor : observation_->systems)
		if (sensor.kind == OperatingSystem::Radar && !sensor.radial_ranges.empty())
		{
			const auto [low, high] = std::minmax_element(sensor.radial_ranges.begin(), sensor.radial_ranges.end());
			text << " " << sensor.id << ": " << localise("Sensor reach") << " " << *low / 1000 << "–" << *high / 1000
			     << " m.";
		}
	for (const auto &b : observation_->bearing_contacts)
		text << " " << localise("Contact") << " " << b.id << "; " << bearing_readout(b.bearing_mdeg) << ".";
	for (const auto &contact : observation_->contacts)
	{
		const double x = static_cast<double>(contact.x - observation_->own.x),
		             y = static_cast<double>(contact.y - observation_->own.y);
		const auto bearing = std::fmod(std::atan2(y, x) * 180 / 3.14159265358979323846 + 360, 360.0);
		text << " " << contact.definition << " #" << contact.id << ", " << static_cast<int>(std::hypot(x, y) / 1000)
		     << " m, bearing " << static_cast<int>(bearing) << " degrees.";
		text << " " << localise("Altitude") << " " << contact.altitude / 1000 << " m.";
		text << " " << localise(contact.faction_label.empty() ? "Unaffiliated" : contact.faction_label) << ", "
		     << localise(contact.disposition) << ".";
	}
	return text.str();
}
} // namespace phoenix
