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
import phoenix.content.assets;

import phoenix.application.preferences;
import phoenix.console_controls;
#include "model_internal.hpp"
namespace phoenix
{
namespace console_detail
{
const Json &borrowed_member(const Json &value, const std::string &key, bool array)
{
	static const Json empty_object = Json::object(), empty_array = Json::array();
	const auto found = value.find(key);
	return found == value.end() ? (array ? empty_array : empty_object) : *found;
}
const Json &controller_rows(const Json &gameplay)
{
	static const Json empty = Json::array();
	const auto rows = gameplay.find("controllers");
	return rows == gameplay.end() ? empty : *rows;
}
std::string bearing_readout(std::int64_t bearing)
{
	std::ostringstream text;
	text << localise("Bearing") << " " << std::fixed << std::setprecision(1) << bearing / 1000.0 << "°; "
	     << localise("Range unknown");
	return text.str();
}
std::optional<std::int64_t> console_integer(const Json &value)
{
	if (!value.is_number_integer() ||
	    (value.is_number_unsigned() &&
	     value.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())))
		return {};
	return value.get<std::int64_t>();
}
std::optional<std::uint64_t> console_unsigned(const Json &value)
{
	if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0))
		return {};
	return value.get<std::uint64_t>();
}
std::string console_string(const Json &value, const char *field)
{
	if (!value.is_object() || !value.contains(field) || !value.at(field).is_string())
		return {};
	return value.at(field).get<std::string>();
}
std::string console_scalar(const Json &value)
{
	if (value.is_string())
		return value.get<std::string>();
	if (value.is_boolean())
		return localise(value.get<bool>() ? "Yes" : "No");
	if (value.is_number_unsigned())
		return std::to_string(value.get<std::uint64_t>());
	if (const auto number = console_integer(value))
		return std::to_string(*number);
	return {};
}
std::string console_label(const std::string &key)
{
	const auto translated = localise(key);
	if (translated != key)
		return translated;
	auto fallback = key;
	std::replace(fallback.begin(), fallback.end(), '_', ' ');
	return fallback;
}
std::string console_execution_prefix(const Json &descriptor)
{
	const auto key =
	    console_string(descriptor, descriptor.contains("process_label") ? "process_label" : "ability_label");
	if (key.empty())
		return {};
	auto prefix = console_label(key);
	if (descriptor.contains("ability_target"))
		if (const auto target = console_unsigned(descriptor.at("ability_target"));
		    target && *target > 0 && *target <= 1000000000ULL)
			prefix += " [" + std::to_string(*target) + "]";
	return prefix + ": ";
}
std::string console_encoded(const std::string &value)
{
	static constexpr char hex[] = "0123456789abcdef";
	std::string result;
	for (const unsigned char byte : value)
	{
		result += hex[byte >> 4];
		result += hex[byte & 15];
	}
	return result;
}
std::optional<std::string> console_decoded(const std::string &value)
{
	if (value.empty() || value.size() > 512 || value.size() % 2)
		return {};
	const auto digit = [](char value) -> int
	{
		if (value >= '0' && value <= '9')
			return value - '0';
		if (value >= 'a' && value <= 'f')
			return value - 'a' + 10;
		return -1;
	};
	std::string result;
	for (std::size_t at = 0; at < value.size(); at += 2)
	{
		const int high = digit(value[at]), low = digit(value[at + 1]);
		if (high < 0 || low < 0)
			return {};
		result += static_cast<char>((high << 4) | low);
	}
	return result;
}
std::string console_message(const Json &message)
{
	const auto substitute = [&](const std::string &key)
	{
		const auto text = localise(key);
		if (translate(key, false) == key && key.find(' ') == std::string::npos &&
		    key.find_first_of("._/") != std::string::npos)
			return std::string{};
		const Json parameters = message.value("parameters", Json::object());
		std::string result;
		for (std::size_t at = 0; at < text.size();)
		{
			if (text[at] == '{')
			{
				const auto end = text.find('}', at + 1);
				if (end != std::string::npos && parameters.is_object())
				{
					const auto name = text.substr(at + 1, end - at - 1);
					if (parameters.contains(name))
					{
						result += console_scalar(parameters.at(name));
						at = end + 1;
						continue;
					}
				}
			}
			result += text[at++];
		}
		if (result.find_first_of("{}") != std::string::npos)
			return std::string{};
		return result;
	};
	std::string result = substitute(console_string(message, "title_key"));
	const auto body = substitute(console_string(message, "body_key"));
	if (!body.empty())
	{
		if (!result.empty())
			result += ": ";
		result += body;
	}
	return result.empty() ? localise("Message unavailable") : result;
}
bool console_descriptor(const Json &descriptor)
{
	return descriptor.is_object() && !console_string(descriptor, "id").empty() &&
	       console_string(descriptor, "id").size() <= 256 && !console_string(descriptor, "action").empty() &&
	       descriptor.contains("value") && console_integer(descriptor.at("value")).has_value() &&
	       (!descriptor.contains("enabled") ||
	        (descriptor.at("enabled").is_boolean() && descriptor.at("enabled").get<bool>()));
}
std::string console_evidence(const Json &record, std::uint64_t now)
{
	std::string text;
	if (record.contains("kind"))
	{
		const auto kind = console_unsigned(record.at("kind"));
		static const std::vector<std::string> kinds{"Fact", "Claim", "Intent", "Prediction"};
		if (kind && *kind < kinds.size())
			text = localise(kinds[static_cast<std::size_t>(*kind)]);
	}
	for (const auto *field : {"source", "observed_tick", "uncertainty"})
		if (record.contains(field))
		{
			if (!text.empty())
				text += "; ";
			text += console_label(field) + " " + console_scalar(record.at(field));
		}
	bool stale = record.contains("stale") && record.at("stale") == true;
	if (record.contains("observed_tick") && record.contains("stale_after_ticks"))
	{
		const auto observed = console_unsigned(record.at("observed_tick")),
		           interval = console_unsigned(record.at("stale_after_ticks"));
		if (observed && interval && *interval > 0 && now > *observed && now - *observed > *interval)
			stale = true;
	}
	if (stale)
		text += "; " + localise("Stale");
	if (record.contains("withdrawn") && record.at("withdrawn") == true)
		text += "; " + localise("Withdrawn");
	return text;
}
std::string console_property_readout(const Json &properties)
{
	std::string text;
	for (const auto &property : properties)
	{
		text += "; " + localise(property.at("label")) + " ";
		if (property.at("kind") == "enum")
			text += localise(property.at("choice_label"));
		else if (property.at("kind") == "flag")
			text += localise(property.at("value").get<bool>() ? "Yes" : "No");
		else
			text += std::to_string(property.at("value").get<std::int64_t>() /
			                       property.at("display_scale").get<std::int64_t>()) +
			        (property.at("unit") == "permille" ? "%" : "");
	}
	return text;
}
std::string console_objective_status(const Json &value)
{
	const auto status = console_unsigned(value);
	static const std::vector<std::string> statuses{"Pending", "Active", "Completed", "Failed", "Cancelled"};
	return status && *status < statuses.size() ? localise(statuses[static_cast<std::size_t>(*status)])
	                                           : localise(console_scalar(value));
}
} // namespace console_detail

namespace console_detail
{
std::vector<std::pair<std::string, std::string>> board_readouts(const Json &board, std::uint64_t now)
{
	std::vector<std::pair<std::string, std::string>> result;
	if (!board.is_object())
		return result;
	const auto omitted = board.value("omitted", Json::object());
	if (omitted.is_object())
		for (auto entry = omitted.begin(); entry != omitted.end(); ++entry)
			if (const auto count = console_unsigned(entry.value()); count && *count > 0)
				result.emplace_back("game.readout/omitted-" + console_encoded(entry.key()),
				                    localise("Entries omitted from network view") + ": " + console_label(entry.key()) +
				                        " " + std::to_string(*count));
	for (const auto *scope : {"records", "shared"})
	{
		const auto records = board.value(scope, Json::array());
		if (!records.is_array())
			continue;
		std::map<std::string, Json> latest;
		for (const auto &record : records)
		{
			if (!record.is_object() || console_string(record, "schema") == "phoenix.camera" ||
			    console_string(record, "schema").starts_with("phoenix.sensor."))
				continue;
			std::string identity = console_string(record, "schema");
			for (const auto *key : {"subject", "source", "publication"})
				identity += "/" + (record.contains(key) ? console_scalar(record.at(key)) : std::string{});
			const auto version = [&](const Json &value)
			{
				return std::pair{
				    value.contains("observed_tick") ? console_unsigned(value.at("observed_tick")).value_or(0)
				                                    : std::uint64_t{0},
				    value.contains("revision") ? console_unsigned(value.at("revision")).value_or(0) : std::uint64_t{0}};
			};
			const auto existing = latest.find(identity);
			if (existing == latest.end() || version(record) >= version(existing->second))
				latest[identity] = record;
		}
		for (const auto &[identity, record] : latest)
		{
			if (console_string(record, "schema") == "phoenix.objective")
			{
				const auto fields = record.value("fields", Json::object());
				const auto title = localise(console_string(fields, "title"));
				const auto description = localise(console_string(fields, "description"));
				std::string text = title + " — " + console_objective_status(fields.value("status", Json{}));
				if (!description.empty() && description != title)
					text += ". " + description;
				result.emplace_back("game.readout/" + std::string(scope) + "-" + console_encoded(identity),
				                    std::move(text));
				continue;
			}
			std::string text = localise(std::string(scope) == "shared" ? "Shipwide" : "Console") + " / " +
			                   console_label(console_string(record, "schema"));
			const auto fields = record.value("fields", Json::object());
			if (fields.is_object())
				for (auto field = fields.begin(); field != fields.end(); ++field)
				{
					std::string value = console_scalar(field.value());
					if ((field.key().ends_with("_key") || field.key() == "title" || field.key() == "description") &&
					    field.value().is_string())
						value = localise(field.value().get<std::string>());
					if (console_string(record, "schema") == "phoenix.objective" && field.key() == "status")
						value = console_objective_status(field.value());
					text += "; " + console_label(field.key()) + " " + value;
				}
			text += "; " + console_evidence(record, now);
			result.emplace_back("game.readout/" + std::string(scope) + "-" + console_encoded(identity),
			                    std::move(text));
		}
	}
	const auto objectives = board.value("objectives", Json::array());
	if (objectives.is_array())
		for (const auto &objective : objectives)
		{
			if (!objective.is_object())
				continue;
			const auto id =
			    objective.contains("id") ? console_scalar(objective.at("id")) : console_string(objective, "handle");
			std::string text = console_message(objective);
			if (text.empty())
				text = localise("Objective") + " " + id;
			for (const auto *key : {"status", "region", "credited"})
				if (objective.contains(key))
					text += "; " + console_label(key) + " " +
					        (std::string(key) == "status" ? console_objective_status(objective.at(key))
					                                      : console_scalar(objective.at(key)));
			text += "; " + console_evidence(objective, now);
			result.emplace_back("game.readout/objective-" + console_encoded(id), std::move(text));
		}

	return result;
}
} // namespace console_detail

using namespace console_detail;
std::string console_control_label(const Json &descriptor)
{
	auto label = console_label(console_string(descriptor, "label_key"));
	if (label.empty())
		label = console_label(console_string(descriptor, "label"));
	if (descriptor.value("label_value", Json(false)) == true)
	{
		const auto value = console_integer(descriptor.at("value"));
		if (!value)
			return {};
		if (descriptor.value("action", "") == "system_enabled")
			label = localise(*value ? "Enable" : "Disable") + " " + label;
		else
			label += " " + std::to_string(*value);
	}
	label = console_execution_prefix(descriptor) + localise(descriptor.value("label_prefix", std::string{})) + label;
	if (descriptor.value("selected", false))
		label += " (" + localise("Selected") + ")";
	return label;
}
std::vector<ConsoleMessageView> console_messages(const Json &board, bool public_only)
{
	std::vector<ConsoleMessageView> result;
	if (!board.is_object())
		return result;
	const auto rows = board.value("messages", Json::array());
	if (!rows.is_array())
		return result;
	for (const auto &message : rows)
	{
		if (!message.is_object() || (public_only && !message.value("public", false)))
			continue;
		ConsoleMessageView view;
		view.id = std::string(public_only ? "viewscreen.message/" : "game.message/") +
		          console_encoded(console_scalar(message.value("sequence", message.value("id", Json{}))));
		const auto console = message.value("sender_console_key", std::string{});
		const auto label = message.value("sender_console_label", console);
		view.sender = localise(label);
		if (view.sender.empty())
			view.sender = localise("Ship");
		// Authored display names are preferred; legacy/custom identifiers get readable words.
		if (view.sender == label)
		{
			for (auto &c : view.sender)
				if (c == '_' || c == '-' || c == '.')
					c = ' ';
			if (!view.sender.empty() && view.sender[0] >= 'a' && view.sender[0] <= 'z')
				view.sender[0] -= 'a' - 'A';
		}
		const std::map<std::string, unsigned> portraits{{"helm", 1},       {"engineering", 2},    {"science", 3},
		                                                {"comms", 4},      {"communications", 4}, {"weapons", 5},
		                                                {"tactical", 5},   {"captain", 6},        {"command", 6},
		                                                {"operations", 3}, {"support", 4},        {"mission", 4}};
		unsigned portrait = 2166136261u;
		for (unsigned char c : console)
			portrait = (portrait ^ c) * 16777619u;
		portrait = portraits.contains(console) ? portraits.at(console) : portrait % 6 + 1;
		view.portrait = "messages/console-" + std::to_string(portrait) + ".png";
		const auto portrait_ref = message.value("speaker_portrait", std::string{});
		if (portrait_ref == "generic-crew")
			view.portrait = "messages/console-1.png";
		if (portrait_ref.starts_with("console-assets:assets/comms/") && portrait_ref.size() < 128)
			view.portrait = portrait_ref;
		const auto name = message.value("speaker_name", std::string{});
		if (!name.empty())
		{
			view.sender = name;
			const auto bytes = message.value("speaker_avatar", Json::array());
			if (bytes.is_array() && bytes.size() <= 12288)
			{
				try
				{
					view.avatar = bytes.get<Bytes>();
					verify_player_avatar(view.avatar);
				}
				catch (...)
				{
					view.avatar.clear();
				}
			}
		}
		view.text = console_message(message);
		result.push_back(std::move(view));
	}
	return result;
}
std::vector<ConsoleMessageView> ConsoleModel::messages() const
{
	return observation_ ? console_messages(observation_->gameplay) : std::vector<ConsoleMessageView>{};
}
std::vector<std::pair<std::string, std::string>> public_board_readouts(const Json &board, std::uint64_t now)
{
	Json selected{{"shared", Json::array()}, {"messages", Json::array()}};
	if (!board.is_object())
		return {};
	const auto shared = board.value("shared", Json::array());
	if (shared.is_array())
	{
		const auto skip = shared.size() > 8 ? shared.size() - 8 : 0;
		for (std::size_t i = skip; i < shared.size(); ++i)
			if (console_string(shared[i], "schema") != "phoenix.objective" &&
			    console_string(shared[i], "schema") != "phoenix.waypoint")
				selected["shared"].push_back(shared[i]);
	}

	auto rows = board_readouts(selected, now);
	if (board.contains("combat_result"))
		rows.emplace_back("game.readout/combat-result", localise(board.at("combat_result")));
	const auto alert = board.value("alert", Json::object());
	if (!alert.empty())
		rows.emplace_back("game.readout/alert", localise("Alert condition") + ": " + localise(alert.at("label")));
	for (auto &[id, text] : rows)
		id.replace(0, std::string("game.readout/").size(), "viewscreen.readout/");
	if (const auto d = board.value("display", Json(nullptr)); d.is_object())
	{
		rows.emplace_back("viewscreen.readout/display", localise("On screen") + ": " + localise(d.at("kind")) + " / " +
		                                                    d.at("console").get<std::string>());
		if (d.value("omitted_contacts", 0) > 0)
			rows.emplace_back("viewscreen.readout/omitted-contacts",
			                  localise("Additional radar contacts omitted") + ": " +
			                      std::to_string(d.at("omitted_contacts").get<int>()));
		for (const auto &b : d.value("bearing_contacts", Json::array()))
			rows.emplace_back("viewscreen.readout/contact/" + std::to_string(b.at("id").get<EntityId>()),
			                  localise("Contact") + " " + std::to_string(b.at("id").get<EntityId>()) + "; " +
			                      bearing_readout(b.at("bearing_mdeg").get<std::int64_t>()));
		for (const auto &contact : d.at("contacts"))
			rows.emplace_back("viewscreen.readout/contact/" + std::to_string(contact.at("id").get<EntityId>()),
			                  localise("Contact") + " " + std::to_string(contact.at("id").get<EntityId>()) + "; X " +
			                      std::to_string(contact.at("x").get<std::int64_t>()) + "; Y " +
			                      std::to_string(contact.at("y").get<std::int64_t>()));
		for (const auto &r : d.at("regions"))
			rows.emplace_back("viewscreen.readout/region/" + r.at("name").get<std::string>(), localise(r.at("name")));
	}
	return rows;
}
} // namespace phoenix
