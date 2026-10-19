module;
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <limits>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

module phoenix.console;
import phoenix.navigation;
namespace phoenix
{
namespace
{
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
	return result;
}
bool console_descriptor(const Json &descriptor)
{
	return descriptor.is_object() && !console_string(descriptor, "id").empty() &&
	       console_string(descriptor, "id").size() <= 256 && !console_string(descriptor, "action").empty() &&
	       descriptor.contains("value") && console_integer(descriptor.at("value")).has_value() &&
	       (!descriptor.contains("enabled") ||
	        (descriptor.at("enabled").is_boolean() && descriptor.at("enabled").get<bool>()));
}
std::string console_evidence(const Json &record, std::uint64_t now = 0)
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
std::string console_objective_status(const Json &value)
{
	const auto status = console_unsigned(value);
	static const std::vector<std::string> statuses{"Pending", "Active", "Completed", "Failed", "Cancelled"};
	return status && *status < statuses.size() ? localise(statuses[static_cast<std::size_t>(*status)])
	                                           : localise(console_scalar(value));
}
} // namespace

namespace
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
			if (!record.is_object())
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
	const auto messages = board.value("messages", Json::array());
	if (messages.is_array())
		for (const auto &message : messages)
		{
			if (!message.is_object())
				continue;
			const auto id = message.contains("sequence") ? console_scalar(message.at("sequence"))
			                                             : console_scalar(message.value("id", Json{}));
			std::string text = console_message(message);
			if (message.value("offer_only", false) && !message.value("public", false))
				text = localise("Work offer") + ": " + text;
			for (const auto *key : {"sender_system", "sender_console", "destination", "delivered_tick", "urgency"})
				if (message.contains(key))
					text += "; " + console_label(key) + " " + console_scalar(message.at(key));
			text += "; " + localise(message.value("actionable", false) ? "Actionable" : "Historical");
			if (message.contains("payload") && message.at("payload").is_object())
			{
				const auto &payload = message.at("payload");
				const auto fields = payload.value("fields", Json::object());
				if (fields.is_object())
					for (auto field = fields.begin(); field != fields.end(); ++field)
						text += "; " + console_label(field.key()) + " " + console_scalar(field.value());
				text += "; " + console_evidence(payload, now);
			}
			result.emplace_back("game.readout/message-" + console_encoded(id), std::move(text));
		}
	return result;
}
} // namespace
std::vector<std::pair<std::string, std::string>> public_board_readouts(const Json &board, std::uint64_t now)
{
	Json selected{{"shared", Json::array()}, {"messages", Json::array()}, {"omitted", Json::object()}};
	if (!board.is_object())
		return {};
	for (const auto *key : {"shared", "messages"})
	{
		auto rows = board.value(key, Json::array());
		if (!rows.is_array())
			continue;
		if (std::string(key) == "messages")
		{
			auto &items = rows.get_ref<Json::array_t &>();
			std::erase_if(items,
			              [](const auto &row)
			              {
				              return !row.is_object() || !row.value("public", false);
			              });
		}
		if (std::string(key) == "messages")
			for (auto &row : rows)
				if (row.is_object())
					row["actionable"] = false;
		const auto skip = rows.size() > 8 ? rows.size() - 8 : 0;
		for (std::size_t i = skip; i < rows.size(); ++i)
			if (std::string(key) != "messages" || (rows[i].is_object() && rows[i].value("public", false)))
				selected[key].push_back(rows[i]);
		const auto omissions = board.value("omitted", Json::object());
		selected["omitted"][key] = skip + (omissions.is_object() ? omissions.value(key, std::size_t{0}) : 0);
	}
	auto rows = board_readouts(selected, now);
	for (auto &[id, text] : rows)
		id.replace(0, std::string("game.readout/").size(), "viewscreen.readout/");
	return rows;
}
void ConsoleModel::reset()
{
	owner_.reset();
	packs_.clear();
	observation_.reset();
	pending_at_.reset();
	pending_requests_.clear();
	held_controls_.clear();
	notification_sequence_ = 0;
	notifications_initialised_ = false;
	speed_ = heading_ = maximum_ = 0;
	layout_ = nullptr;
	announcement_.clear();
}
void ConsoleModel::refresh(const std::shared_ptr<SessionPort> &session, std::uint64_t route)
{
	if (!session)
	{
		reset();
		return;
	}
	const auto &catalog = session->catalog();
	if (owner_.lock() != session || generation_ != session->generation() || packs_ != catalog.ordered_packs ||
	    route_ != route)
	{
		reset();
		owner_ = session;
		generation_ = session->generation();
		route_ = route;
		packs_ = catalog.ordered_packs;
	}
	auto next = session->console_observation(route);
	const bool changed = observation_.has_value() != next.has_value() ||
	                     (next && observation_ &&
	                      (next->own.id != observation_->own.id || next->console != observation_->console ||
	                       next->controls != observation_->controls || next->feeds != observation_->feeds));
	if (changed)
	{
		pending_at_.reset();
		pending_requests_.clear();
		held_controls_.clear();
		announcement_.clear();
		notification_sequence_ = 0;
		notifications_initialised_ = false;
		layout_ = nullptr;
		speed_ = heading_ = maximum_ = 0;
		if (next)
		{
			for (const auto &seat : resolve_consoles(catalog, next->own.definition))
				if (seat.id == next->console || next->console.empty())
				{
					if (catalog.consoles.contains(seat.definition))
						layout_ = catalog.consoles.at(seat.definition);
					break;
				}
			std::string binding;
			if (!layout_.is_null())
				for (const auto &widget : layout_.at("widgets"))
					if (widget.at("kind") == "throttle")
						binding = widget.value("system", std::string{});
			if (const auto selected = select_system(next->systems, OperatingSystem::Engine, binding))
				for (const auto &system : next->systems)
					if (system.id == *selected)
						maximum_ = system.maximum;
		}
	}
	observation_ = std::move(next);
	if (observation_ && observation_->gameplay.is_object())
	{
		const auto notifications = observation_->gameplay.value("notifications", Json::array());
		std::string notification;
		if (notifications.is_array())
			for (const auto &message : notifications)
			{
				if (!message.is_object() || !message.contains("sequence"))
					continue;
				const auto sequence = console_unsigned(message.at("sequence"));
				if (!sequence || *sequence == 0)
					continue;
				if (*sequence > notification_sequence_)
				{
					if (notifications_initialised_)
					{
						if (!notification.empty())
							notification += " ";
						notification += console_message(message);
					}
					notification_sequence_ = *sequence;
				}
			}
		notifications_initialised_ = true;
		if (!notification.empty())
			announcement_ = std::move(notification);
	}
	if (observation_ && (!pending_at_ || session->ticks() > *pending_at_))
	{
		pending_requests_.clear();
		speed_ = observation_->requested_speed;
		heading_ = observation_->requested_heading;
		if (!layout_.is_null())
			for (const auto &widget : layout_.at("widgets"))
			{
				const auto id = widget.at("id").get<std::string>();
				if (widget.at("kind") == "throttle")
					speed_ = system_request(widget_system(id), "speed_mm_s");
				else if (widget.at("kind") == "heading")
					heading_ = system_request(widget_system(id), "heading_mdeg");
			}
		pending_at_.reset();
		pending_requests_.clear();
	}
}
bool ConsoleModel::act(const std::string &id)
{
	auto session = owner_.lock();
	if (!session)
		return false;
	refresh(session, route_);
	if (!observation_)
		return false;
	if (id.starts_with("game.control/"))
	{
		const auto decoded = console_decoded(id.substr(13));
		if (!decoded || !observation_->gameplay.is_object())
			return false;
		const auto controls = observation_->gameplay.value("controls", Json::array());
		const Json *selected = nullptr;
		if (controls.is_array())
			for (const auto &descriptor : controls)
				if (console_descriptor(descriptor) && console_string(descriptor, "id") == *decoded)
				{
					if (selected)
						return false;
					selected = &descriptor;
				}
		if (!selected)
			return false;
		if (!console_string(*selected, "controller").empty())
			return submit(decode_operating_command(*selected));
		return command(console_string(*selected, "action"), *console_integer(selected->at("value")),
		               console_string(*selected, "system"));
	}
	const auto control = id.substr(0, id.find('/'));
	if (control == "game.contacts")
	{
		if (!(observation_->feeds & system_bit(OperatingSystem::Radar)))
			return false;
		announcement_ = contacts_text();
		return true;
	}
	OperatingCommand command{observation_->own.id, {}, 0, {}};
	const auto separator = id.find('/');
	const auto binding = separator == std::string::npos ? std::string{} : widget_system(id.substr(separator + 1));
	if (control == "game.faster" || control == "game.slower")
	{
		command.action = "speed_mm_s";
		command.system = binding;
		const auto speed = binding.empty() ? speed_ : system_request(binding, "speed_mm_s");
		const auto maximum = binding.empty() ? maximum_ : system_maximum(binding);
		const auto minimum = system_minimum(binding);
		command.value = std::clamp(
		    speed + (control == "game.faster" ? 1 : -1) * std::max<std::int64_t>(1, maximum / 20), minimum, maximum);
	}
	else if (control == "game.left" || control == "game.right")
	{
		command.action = "heading_mdeg";
		command.system = binding;
		command.value = ((binding.empty() ? heading_ : system_request(binding, "heading_mdeg")) +
		                 (control == "game.left" ? 5000 : 355000)) %
		                360000;
	}
	else
		return false;
	return submit(std::move(command));
}
std::string ConsoleModel::widget_system(const std::string &id) const
{
	if (!observation_ || layout_.is_null())
		return {};
	for (const auto &widget : layout_.at("widgets"))
		if (widget.at("id") == id)
		{
			const auto kind = widget.at("kind");
			const auto type = kind == "throttle"  ? OperatingSystem::Engine
			                  : kind == "heading" ? OperatingSystem::Steering
			                                      : OperatingSystem::Radar;
			return select_system(observation_->systems, type, widget.value("system", std::string{}), kind != "radar")
			    .value_or("");
		}
	return {};
}
std::int64_t ConsoleModel::system_maximum(const std::string &id) const
{
	if (observation_)
		for (const auto &system : observation_->systems)
			if (system.id == id)
				return system.maximum;
	return 0;
}
std::int64_t ConsoleModel::system_minimum(const std::string &id) const
{
	if (!observation_)
		return 0;
	std::string target = id;
	if (target.empty())
		target = select_system(observation_->systems, OperatingSystem::Engine).value_or("");
	for (const auto &system : observation_->systems)
		if (system.id == target)
		{
			const auto found = system.telemetry.find("minimum_speed");
			if (found != system.telemetry.end())
				return std::clamp(found->second, -system.maximum, std::int64_t{0});
		}
	return 0;
}
std::int64_t ConsoleModel::system_request(const std::string &id, const std::string &action) const
{
	if (observation_)
		for (const auto &system : observation_->systems)
			if (system.id == id)
			{
				const auto channel =
				    action.empty() ? system.kind == OperatingSystem::Steering ? "heading_mdeg" : "speed_mm_s" : action;
				if (auto found = pending_requests_.find({id, channel}); found != pending_requests_.end())
					return found->second;
				const auto field = channel == "heading_mdeg" ? "desired_heading"
				                   : channel == "speed_mm_s" ? "desired_speed"
				                                             : channel;
				if (const auto found = system.telemetry.find(field); found != system.telemetry.end())
					return found->second;
				if ((channel == "heading_mdeg") == (system.kind == OperatingSystem::Steering))
					return system.requested;
				return 0;
			}
	return 0;
}
bool ConsoleModel::set_speed_fraction(double fraction, const std::string &system)
{
	if (!std::isfinite(fraction) || fraction < 0 || fraction > 1)
		return false;
	auto session = owner_.lock();
	if (!session)
		return false;
	refresh(session, route_);
	const auto minimum = system_minimum(system), maximum = system.empty() ? maximum_ : system_maximum(system);
	return observation_ &&
	       submit({observation_->own.id, "speed_mm_s",
	               static_cast<std::int64_t>(std::llround(minimum + (maximum - minimum) * fraction)), system});
}
bool ConsoleModel::set_heading_degrees(double degrees, const std::string &system)
{
	if (!std::isfinite(degrees) || degrees < 0 || degrees >= 360)
		return false;
	auto session = owner_.lock();
	if (!session)
		return false;
	refresh(session, route_);
	return observation_ && submit({observation_->own.id, "heading_mdeg",
	                               static_cast<std::int64_t>(std::llround(degrees * 1000)) % 360000, system});
}
bool ConsoleModel::command(const std::string &action, std::int64_t value, const std::string &system)
{
	auto session = owner_.lock();
	if (!session)
		return false;
	refresh(session, route_);
	return observation_ && submit({observation_->own.id, action, value, system});
}
bool ConsoleModel::hold_control(const std::string &id)
{
	auto session = owner_.lock();
	if (!session || !id.starts_with("game.control/"))
		return false;
	refresh(session, route_);
	if (!observation_ || !observation_->gameplay.is_object())
		return false;
	const auto decoded = console_decoded(id.substr(13));
	if (!decoded)
		return false;
	const auto controls = observation_->gameplay.value("controls", Json::array());
	const Json *selected = nullptr;
	if (controls.is_array())
		for (const auto &descriptor : controls)
			if (console_descriptor(descriptor) && console_string(descriptor, "id") == *decoded)
			{
				if (selected)
					return false;
				selected = &descriptor;
			}
	if (!selected)
		return false;
	const auto action = console_string(*selected, "action");
	if (action != "longitudinal_thrust" && action != "lateral_thrust" && action != "manual_steering")
		return false;
	const auto value = *console_integer(selected->at("value"));
	if (!value)
		return false;
	OperatingCommand request{observation_->own.id, action, value, console_string(*selected, "system")};
	if (!console_string(*selected, "controller").empty())
		request = decode_operating_command(*selected);
	if (request.system.empty() && request.controller.empty())
	{
		const auto selected_system = select_system(observation_->systems, operating_action(action)->system);
		if (!selected_system)
			return false;
		request.system = *selected_system;
	}
	if (!submit(request))
		return false;
	std::erase_if(held_controls_,
	              [&](const auto &held)
	              {
		              return held.second.action == request.action && held.second.system == request.system &&
		                     held.second.controller == request.controller && held.second.channel == request.channel;
	              });
	held_controls_[id] = std::move(request);
	return true;
}
bool ConsoleModel::release_control(const std::string &id)
{
	const auto found = held_controls_.find(id);
	if (found == held_controls_.end())
		return false;
	auto request = found->second;
	request.value = 0;
	held_controls_.erase(found);
	return submit(std::move(request));
}
void ConsoleModel::release_controls()
{
	const auto held = held_controls_;
	held_controls_.clear();
	for (const auto &[id, original] : held)
	{
		auto request = original;
		request.value = 0;
		submit(std::move(request));
	}
}
bool ConsoleModel::submit(OperatingCommand command)
{
	auto session = owner_.lock();
	if (!session || !observation_)
		return false;
	const auto *spec = operating_action(command.action);
	std::string projected_label;
	bool projected = false;
	if (observation_->gameplay.is_object())
	{
		const auto descriptors = observation_->gameplay.value("controls", Json::array());
		if (descriptors.is_array())
			for (const auto &descriptor : descriptors)
				if (console_descriptor(descriptor) && console_string(descriptor, "action") == command.action &&
				    console_string(descriptor, "system") == command.system &&
				    console_string(descriptor, "controller") == command.controller &&
				    console_string(descriptor, "channel") == command.channel &&
				    descriptor.value("generation", std::uint64_t{0}) == command.generation &&
				    descriptor.value("request_id", std::uint64_t{0}) == command.request_id &&
				    *console_integer(descriptor.at("value")) == command.value)
				{
					projected = true;
					projected_label = console_label(console_string(descriptor, "label_key"));
					if (projected_label.empty())
						projected_label = console_label(console_string(descriptor, "label"));
				}
	}
	if ((!spec || !command.controller.empty()) && !projected)
		return false;
	if (spec && command.controller.empty())
	{
		if (!operating_value_valid(*spec, command.value))
			return false;
		if (command.system.empty())
		{
			const auto selected = select_system(observation_->systems, spec->system);
			if (!selected)
				return false;
			command.system = *selected;
		}
		else
		{
			const auto selected = std::find_if(observation_->systems.begin(), observation_->systems.end(),
			                                   [&](const SystemView &system)
			                                   {
				                                   return system.id == command.system && system.controlled;
			                                   });
			if (selected == observation_->systems.end() && !projected)
				return false;
		}
	}
	if (!session->console_command(route_, command))
	{
		announcement_ = localise("The server refused that control request");
		return false;
	}
	if (command.controller.empty() && command.action == "speed_mm_s")
		speed_ = command.value;
	else if (command.controller.empty() && command.action == "heading_mdeg")
		heading_ = command.value;
	if (command.controller.empty() && (command.action == "speed_mm_s" || command.action == "heading_mdeg"))
	{
		pending_at_ = session->ticks();
		pending_requests_[{command.system, command.action}] = command.value;
		announcement_ = localise("Requested speed") + " " + std::to_string(speed_ / 1000.0) + " " + localise("m/s") +
		                "; " + localise("heading") + " " + std::to_string(heading_ / 1000) + " " + localise("degrees") +
		                ".";
	}
	else
		announcement_ = localise("Request sent") + (projected_label.empty() ? std::string{} : ": " + projected_label);
	return true;
}
std::vector<std::pair<std::string, std::string>> ConsoleModel::gameplay_controls() const
{
	std::vector<std::pair<std::string, std::string>> result;
	if (!observation_ || !observation_->gameplay.is_object())
		return result;
	const auto controls = observation_->gameplay.value("controls", Json::array());
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
		const auto id = console_string(descriptor, "id");
		if (occurrences[id] != 1)
			continue;
		auto label = console_label(console_string(descriptor, "label_key"));
		if (label.empty())
			label = console_label(console_string(descriptor, "label"));
		if (label.empty())
			continue;
		result.emplace_back("game.control/" + console_encoded(id), std::move(label));
	}
	return result;
}
std::vector<std::pair<std::string, std::string>> ConsoleModel::gameplay_readouts() const
{
	std::vector<std::pair<std::string, std::string>> result;
	if (!observation_)
		return result;
	const auto session = owner_.lock();
	const auto now = session ? session->ticks() : 0;
	for (const auto &system : observation_->systems)
	{
		if (system.telemetry.empty() && system.status.empty())
			continue;
		std::string text = system.id + ": " + console_label(system.status);
		text += "; " + localise("Requested") + " " + std::to_string(system.requested);
		for (const auto &[key, value] : system.telemetry)
			text += "; " + console_label(key) + " " + std::to_string(value);
		if (system.telemetry.contains("depletion_ticks") || system.telemetry.contains("coolant_depletion_ticks"))
			text +=
			    "; " +
			    localise("Estimates assume current demand and supply remain constant; future commands are excluded.");
		result.emplace_back("game.readout/system-" + console_encoded(system.id), std::move(text));
	}
	for (auto row : board_readouts(observation_->gameplay, now))
		result.push_back(std::move(row));
	return result;
}

std::string ConsoleModel::contacts_text() const
{
	if (!observation_ || !(observation_->feeds & system_bit(OperatingSystem::Radar)))
		return {};
	std::ostringstream text;
	text << "Radar: " << observation_->contacts.size() << " contacts.";
	for (const auto &contact : observation_->contacts)
	{
		const double x = static_cast<double>(contact.x - observation_->own.x),
		             y = static_cast<double>(contact.y - observation_->own.y);
		const auto bearing = std::fmod(std::atan2(y, x) * 180 / 3.14159265358979323846 + 360, 360.0);
		text << " " << contact.definition << " #" << contact.id << ", " << static_cast<int>(std::hypot(x, y) / 1000)
		     << " m, bearing " << static_cast<int>(bearing) << " degrees.";
	}
	return text.str();
}
} // namespace phoenix
