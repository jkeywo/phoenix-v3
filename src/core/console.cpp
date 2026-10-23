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
import phoenix.navigation;
import phoenix.console_controls;
namespace phoenix
{
namespace
{
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
} // namespace

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
	Json selected{{"shared", Json::array()}, {"messages", Json::array()}, {"omitted", Json::object()}};
	if (!board.is_object())
		return {};
	const auto shared = board.value("shared", Json::array());
	if (shared.is_array())
	{
		const auto skip = shared.size() > 8 ? shared.size() - 8 : 0;
		for (std::size_t i = skip; i < shared.size(); ++i)
			selected["shared"].push_back(shared[i]);
		const auto omitted = board.value("omitted", Json::object());
		selected["omitted"]["shared"] = skip + (omitted.is_object() ? omitted.value("shared", std::size_t{0}) : 0);
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
		for (const auto &o : d.value("objectives", Json::array()))
			rows.emplace_back("viewscreen.readout/objective/" + std::to_string(o.at("id").get<std::uint64_t>()),
			                  localise(o.at("title")) + "; X " +
			                      std::to_string(o.at("destination_x").get<std::int64_t>()) + "; Y " +
			                      std::to_string(o.at("destination_y").get<std::int64_t>()));
		if (const auto w = d.value("waypoint", Json::object()); w.value("active", false))
			rows.emplace_back("viewscreen.readout/waypoint",
			                  localise("Waypoint") + "; X " + std::to_string(w.at("x").get<std::int64_t>()) + "; Y " +
			                      std::to_string(w.at("y").get<std::int64_t>()));
	}
	return rows;
}
void ConsoleModel::reset()
{
	combat_target_ = 0;
	selected_compartment_ = 0;
	selected_team_.clear();
	selected_team_order_.clear();
	selected_patient_.clear();
	fire_arcs_ = true;
	owner_.reset();
	packs_.clear();
	observation_.reset();
	pending_at_.reset();
	pending_requests_.clear();
	held_controls_.clear();
	notification_sequence_ = 0;
	notifications_initialised_ = false;
	speed_ = heading_ = maximum_ = 0;
	if (!layout_.is_null())
		++layout_revision_;
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
	available_consoles_.clear();
	if (next)
	{
		available_consoles_.emplace_back(next->console, next->console);
		for (const auto &extra : next->additional_consoles)
			available_consoles_.emplace_back(extra.console, extra.console);
		if (!selected_console_.empty() && selected_console_ != next->console)
		{
			const auto extra = std::find_if(next->additional_consoles.begin(), next->additional_consoles.end(),
			                                [&](const auto &view)
			                                {
				                                return view.console == selected_console_;
			                                });
			if (extra != next->additional_consoles.end())
			{
				auto view = *extra;
				next = std::move(view);
			}
			else
				selected_console_.clear();
		}
	}
	const bool changed = observation_.has_value() != next.has_value() ||
	                     (next && observation_ &&
	                      (next->own.id != observation_->own.id || next->console != observation_->console ||
	                       next->controls != observation_->controls || next->feeds != observation_->feeds));
	if (changed)
	{
		++layout_revision_;
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
					{
						layout_ = catalog.consoles.at(seat.definition);
						if (route == 31 && layout_.contains("compact"))
							layout_["widgets"] = layout_.at("compact").at("widgets");
					}
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
	const auto previous_alert = observation_ ? observation_->gameplay.value("alert", Json::object()) : Json::object();
	observation_ = std::move(next);
	if (observation_)
	{
		const auto alert = observation_->gameplay.value("alert", Json::object());
		if (!previous_alert.empty() && !alert.empty() && previous_alert.at("revision") != alert.at("revision"))
			announcement_ = localise("Alert condition") + ": " + localise(alert.at("label"));
	}
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
						const auto views = console_messages(Json{{"messages", Json::array({message})}});
						if (!views.empty())
							notification += views.front().accessible_text();
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
				{
					speed_ = system_request(widget_system(id), "speed_mm_s");
					maximum_ = system_maximum(widget_system(id));
				}
				else if (widget.at("kind") == "heading")
					heading_ = system_request(widget_system(id), "heading_mdeg");
			}
		pending_at_.reset();
		pending_requests_.clear();
	}
}
bool ConsoleModel::act(const std::string &id)
{
	if (id == "game.status-close")
	{
		status_system_.clear();
		return true;
	}
	if (id.starts_with("game.status/"))
	{
		status_system_ = id.substr(12);
		return true;
	}
	if (id == "game.contact-arcs")
	{
		contact_arcs_ = !contact_arcs_;
		return true;
	}
	if (id.starts_with("game.sensor-contact/"))
	{
		sensor_contact_ = std::stoll(id.substr(20));
		return true;
	}
	if (id == "game.fire-arcs")
	{
		fire_arcs_ = !fire_arcs_;
		return true;
	}
	if (id.starts_with("game.combat-target/"))
	{
		combat_target_ = std::stoll(id.substr(19));
		return true;
	}
	if (id == "game.teams-close")
	{
		selected_team_.clear();
		selected_team_order_.clear();
		selected_patient_.clear();
		return true;
	}
	if (id == "game.teams")
	{
		if (!observation_)
			return false;
		for (const auto &t : observation_->gameplay.value("teams", Json::array()))
			if (!t.at("reserved").get<bool>() && !t.at("orders").empty())
			{
				selected_team_ = t.at("id");
				selected_team_order_ = t.at("orders")[0].at("id");
				selected_patient_.clear();
				return true;
			}
		return false;
	}
	if (id.starts_with("game.team-order/"))
	{
		const auto choice = id.substr(16);
		const auto slash = choice.find('/');
		if (slash == std::string::npos)
			return false;
		selected_team_ = choice.substr(0, slash);
		selected_team_order_ = choice.substr(slash + 1);
		selected_patient_.clear();
		return true;
	}
	if (id.starts_with("game.team-cancel/"))
	{
		const auto team = id.substr(17);
		const bool accepted = team_action("cancel", team, {});
		if (accepted && team == selected_team_)
		{
			selected_team_.clear();
			selected_team_order_.clear();
			selected_patient_.clear();
		}
		return accepted;
	}
	if (id.starts_with("game.team-patient/"))
	{
		selected_patient_ = selected_patient_ == id.substr(18) ? std::string{} : id.substr(18);
		return true;
	}
	if (id.starts_with("game.compartment/"))
	{
		selected_compartment_ = std::stoll(id.substr(17));
		if (!selected_team_.empty() && !selected_team_order_.empty())
			for (const auto &c : team_map())
				if (c.at("index") == selected_compartment_ && c.value("eligible", false))
					return team_action("order", selected_team_,
					                   selected_team_order_ + "/" + c.at("id").get<std::string>() +
					                       (selected_patient_.empty() ? std::string{} : "/" + selected_patient_));
		return true;
	}
	if (id.starts_with("game.console/"))
	{
		selected_console_ = id.substr(13);
		return true;
	}
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
		if (selected->value("momentary", false))
			return held_controls_.contains(id) ? release_control(id) : hold_control(id);
		if (!console_string(*selected, "controller").empty())
			return submit(decode_operating_command(*selected));
		if (console_string(*selected, "action") == "stance.set")
		{
			OperatingCommand intent{observation_->own.id, "stance.set", selected->at("value"),
			                        console_string(*selected, "system")};
			intent.generation = selected->value("generation", std::uint64_t{0});
			return submit(std::move(intent));
		}
		return command(console_string(*selected, "action"), *console_integer(selected->at("value")),
		               console_string(*selected, "system"));
	}
	const auto control = id.substr(0, id.find('/'));
	if (manual_active() &&
	    (control == "game.faster" || control == "game.slower" || control == "game.left" || control == "game.right"))
		return false;
	if (control == "game.contacts")
	{
		if (!(observation_->feeds & system_bit(OperatingSystem::Radar)))
			return false;
		announcement_ = contacts_text();
		return true;
	}
	OperatingCommand command{observation_->own.id, {}, 0, {}};
	const auto separator = id.find('/');
	const auto binding =
	    separator == std::string::npos
	        ? default_binding(control == "game.faster" || control == "game.slower" ? "speed_mm_s" : "heading_mdeg")
	        : widget_system(id.substr(separator + 1));
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
			if (widget.contains("controller") && widget.contains("channel"))
				return "controller/" + widget.at("controller").get<std::string>() + "/" +
				       widget.at("channel").get<std::string>();
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
	if (const auto channel = controller_channel(id))
		return channel->value("maximum", std::int64_t{0});
	if (observation_)
		for (const auto &system : observation_->systems)
			if (system.id == id)
				return system.maximum;
	return 0;
}
std::int64_t ConsoleModel::system_minimum(const std::string &id) const
{
	if (const auto channel = controller_channel(id))
		return channel->value("minimum", std::int64_t{0});
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
	if (const auto channel = controller_channel(id))
	{
		const auto name = channel->value("action", std::string{});
		if (!action.empty() && action != name)
			return 0;
		if (const auto pending = pending_requests_.find({id, name}); pending != pending_requests_.end())
			return pending->second;
		return channel->value("value", std::int64_t{0});
	}
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
	const auto binding = system.empty() ? default_binding("speed_mm_s") : system;
	const auto minimum = system_minimum(binding), maximum = binding.empty() ? maximum_ : system_maximum(binding);
	return observation_ &&
	       submit({observation_->own.id, "speed_mm_s",
	               static_cast<std::int64_t>(std::llround(minimum + (maximum - minimum) * fraction)), binding});
}
bool ConsoleModel::is_on_screen(const std::string &widget) const
{
	const auto session = owner_.lock();
	if (!session || !observation_)
		return false;
	const auto value = session->public_board().value("display", Json(nullptr));
	return value.is_object() && value.value("console", "") == observation_->console &&
	       value.value("widget", "") == widget && value.at("own").at("id") == observation_->own.id;
}
bool ConsoleModel::on_screen(const std::string &widget, const std::vector<std::int64_t> &camera, bool update)
{
	const auto session = owner_.lock();
	if (!session)
		return false;
	refresh(session, route_);
	if (!observation_ || layout_.is_null())
		return false;
	bool supported = false;
	for (const auto &w : layout_.at("widgets"))
		if (w.at("id") == widget &&
		    (w.at("kind") == "radar" || w.at("kind") == "navigation" || w.at("kind") == "conversations"))
			supported = true;
	if (!supported || (update && !is_on_screen(widget)))
		return false;
	OperatingCommand command{observation_->own.id, update ? "display.update" : "display.toggle", 0, widget};
	command.console = observation_->console;
	command.arguments = camera;
	if (update)
		command.generation = session->public_board().at("display").at("revision");
	const auto eligible = eligible_console_command(*observation_, layout_, session->public_board(), command);
	const bool accepted = eligible && session->console_command(route_, eligible->command);
	if (!update)
		announcement_ = localise(accepted ? "Viewscreen request sent" : "The server refused that control request");
	return accepted;
}
bool ConsoleModel::set_waypoint(std::int64_t x, std::int64_t y, bool active)
{
	const auto session = owner_.lock();
	if (!session || !observation_ || !observation_->gameplay.contains("controllers"))
		return false;
	for (const auto &controller : observation_->gameplay.at("controllers"))
		if (controller.value("human", false))
			for (auto channel = controller.at("commands").begin(); channel != controller.at("commands").end();
			     ++channel)
				if (channel.value().at("action") == "waypoint")
				{
					OperatingCommand command{
					    observation_->own.id,       "waypoint", active ? 1 : 0, {}, controller.at("id"), channel.key(),
					    controller.at("generation")};
					command.arguments = {x, y};
					command.console = observation_->console;
					return operating_arguments_valid(OperatingAction::Waypoint, command.value, command.arguments) &&
					       session->console_command(route_, command);
				}
	return false;
}
Json ConsoleModel::objectives() const
{
	std::map<std::uint64_t, Json> latest;
	if (observation_)
		for (const auto &record : observation_->gameplay.value("shared", Json::array()))
			if (record.value("schema", "") == "phoenix.objective")
			{
				const auto &fields = record.at("fields");
				latest[fields.at("id").get<std::uint64_t>()] = fields;
			}
	Json result = Json::array();
	for (const auto &[key, fields] : latest)
		result.push_back(fields);
	return result;
}
ShipCameraState ConsoleModel::camera() const
{
	return observation_ ? ship_camera_state(observation_->gameplay) : ShipCameraState{};
}
Json ConsoleModel::waypoint() const
{
	Json result = Json::object();
	if (observation_)
		for (const auto &record : observation_->gameplay.value("shared", Json::array()))
			if (record.value("schema", "") == "phoenix.waypoint")
				result = record.at("fields");
	return result;
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
	                               static_cast<std::int64_t>(std::llround(degrees * 1000)) % 360000,
	                               system.empty() ? default_binding("heading_mdeg") : system});
}
bool ConsoleModel::command(const std::string &action, std::int64_t value, const std::string &system)
{
	auto session = owner_.lock();
	if (!session)
		return false;
	refresh(session, route_);
	return observation_ && submit({observation_->own.id, action, value, system});
}
// Resolve sibling commands by their physical consumers, never by channel names.
std::string ConsoleModel::manual_binding(const std::string &action, const std::string &widget_action) const
{
	return observation_ ? manual_console_binding(*observation_, layout_, action, widget_action) : std::string{};
}
bool ConsoleModel::manual_available() const
{
	return !manual_binding("longitudinal_thrust", "speed_mm_s").empty() &&
	       !manual_binding("manual_steering", "heading_mdeg").empty() &&
	       !manual_binding("assisted", "speed_mm_s").empty() && !manual_binding("assisted", "heading_mdeg").empty();
}
bool ConsoleModel::manual_active() const
{
	if (!manual_available())
		return false;
	for (const auto *anchor : {"speed_mm_s", "heading_mdeg"})
	{
		const auto channel = controller_channel(manual_binding("assisted", anchor));
		if (!channel || channel->value("value", 1) || channel->value("pending", true) ||
		    !channel->value("accepted", false))
			return false;
		// Navigation requests can re-enable physical assistance without rewriting the mode channel.
		for (const auto &consumer : channel->at("consumers"))
			for (const auto &system : observation_->systems)
				if (system.id == consumer.get<std::string>() && system.telemetry.contains("assisted") &&
				    system.telemetry.at("assisted"))
					return false;
	}
	return true;
}
bool ConsoleModel::set_manual(bool enabled)
{
	auto session = owner_.lock();
	if (!session)
		return false;
	refresh(session, route_);
	if (!manual_available())
	{
		announcement_ = localise("Manual flight control unavailable");
		return false;
	}
	release_controls();
	std::vector<std::string> sent;
	for (const auto *anchor : {"speed_mm_s", "heading_mdeg"})
	{
		const auto binding = manual_binding("assisted", anchor);
		if (std::find(sent.begin(), sent.end(), binding) != sent.end())
			continue;
		if (!command("assisted", enabled ? 0 : 1, binding))
		{
			for (const auto &previous : sent)
				command("assisted", 1, previous);
			announcement_ = localise("Manual flight mode request refused");
			return false;
		}
		sent.push_back(binding);
	}
	return true;
}
bool ConsoleModel::set_manual_axes(int yaw, int thrust)
{
	auto session = owner_.lock();
	if (!session || yaw < -1000 || yaw > 1000 || thrust < -1000 || thrust > 1000)
		return false;
	refresh(session, route_);
	if (!manual_active())
		return false;
	for (const auto &[action, anchor, value] : std::vector<std::tuple<std::string, std::string, int>>{
	         {"longitudinal_thrust", "speed_mm_s", thrust}, {"manual_steering", "heading_mdeg", yaw}})
	{
		const auto binding = manual_binding(action, anchor);
		OperatingCommand request{observation_->own.id, action, value, binding};
		if (!submit(request))
		{
			release_controls();
			return false;
		}
		const auto section = controller_section(binding);
		request.controller = section->at("id");
		request.channel = binding.substr(binding.find('/', 11) + 1);
		request.generation = section->at("generation");
		request.system.clear();
		if (value)
			held_controls_["manual/" + action] = request;
		else
			held_controls_.erase("manual/" + action);
	}
	return true;
}
bool ConsoleModel::flight_button(const std::string &action, std::int64_t value, bool pressed)
{
	if (!observation_ || !observation_->gameplay.is_object())
		return false;
	bool accepted = false;
	if (action == "impulse" && value == -1)
		value = std::any_of(observation_->systems.begin(), observation_->systems.end(),
		                    [](const auto &system)
		                    {
			                    return system.telemetry.contains("impulse_phase") &&
			                           system.telemetry.at("impulse_phase") != 0;
		                    })
		            ? 0
		            : 1;
	const auto descriptors = observation_->gameplay.value("controls", Json::array());
	const bool coordinated = action == "impulse" && std::any_of(descriptors.begin(), descriptors.end(),
	                                                            [](const auto &d)
	                                                            {
		                                                            return d.value("action", "") == "flight.impulse";
	                                                            });
	for (const auto &descriptor : descriptors)
		if ((coordinated ? descriptor.value("action", "") == "flight.impulse"
		                 : descriptor.value("action", "") == action) &&
		    descriptor.value("value", std::int64_t{0}) == value)
		{
			const auto id = "game.control/" + console_encoded(descriptor.at("id").get<std::string>());
			accepted |= action == "impulse" ? (pressed && act(id)) : pressed ? hold_control(id) : release_control(id);
		}
	return accepted;
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
	if (action != "longitudinal_thrust" && action != "lateral_thrust" && action != "manual_steering" &&
	    action != "boost")
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
	const auto previous = held_controls_;
	held_controls_[id] = request;
	if (action == "lateral_thrust")
	{
		request.value = 0;
		for (const auto &[key, held] : held_controls_)
			if (held.action == action && held.system == request.system && held.controller == request.controller &&
			    held.channel == request.channel)
				request.value += held.value;
		request.value = std::clamp<std::int64_t>(request.value, -1000, 1000);
	}
	if (!submit(request))
	{
		held_controls_ = previous;
		return false;
	}
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
	for (const auto &[key, held] : held_controls_)
		if (held.action == request.action && held.system == request.system && held.controller == request.controller &&
		    held.channel == request.channel)
			request.value = request.action == "lateral_thrust"
			                    ? std::clamp<std::int64_t>(request.value + held.value, -1000, 1000)
			                    : held.value;
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
std::string ConsoleModel::default_binding(const std::string &action) const
{
	std::string result;
	if (layout_.is_null())
		return result;
	for (const auto &widget : layout_.at("widgets"))
		if ((widget.at("kind") == "throttle" && action == "speed_mm_s") ||
		    (widget.at("kind") == "heading" && action == "heading_mdeg"))
		{
			const auto binding = widget_system(widget.at("id"));
			if (!result.empty() && binding != result)
				return {};
			result = binding;
		}
	return result;
}
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
bool ConsoleModel::logistics_action(const std::string &action, const std::string &system, const std::string &text)
{
	if (!observation_)
		return false;
	const auto board = logistics();
	if (!board.contains("revision"))
		return false;
	for (const auto &ctrl : observation_->gameplay.value("controllers", Json::array()))
		if (ctrl.value("human", false))
		{
			OperatingCommand c{observation_->own.id, "logistics." + action, board.at("revision").get<std::int64_t>(),
			                   system};
			c.console = observation_->console;
			c.controller = ctrl.at("id");
			c.generation = ctrl.at("generation");
			c.text = text;
			if (const auto session = owner_.lock(); session && session->console_command(route_, c))
				return true;
		}
	return false;
}
bool ConsoleModel::docking_action(const std::string &action, const std::string &system, const std::string &text)
{
	if (!observation_)
		return false;
	const auto board = docking();
	if (!board.contains("revision"))
		return false;
	for (const auto &ctrl : observation_->gameplay.value("controllers", Json::array()))
		if (ctrl.value("human", false))
		{
			OperatingCommand c{observation_->own.id, "dock." + action, board.at("revision").get<std::int64_t>(),
			                   system};
			c.console = observation_->console;
			c.controller = ctrl.at("id");
			c.generation = ctrl.at("generation");
			c.text = text;
			if (const auto session = owner_.lock(); session && session->console_command(route_, c))
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
			for (const auto &ctrl : observation_->gameplay.value("controllers", Json::array()))
				if (ctrl.value("human", false) || ctrl.value("craft_authority", false))
				{
					OperatingCommand command{observation_->own.id, "craft." + action,
					                         c.at("revision").get<std::int64_t>(), craft_id};
					command.console = observation_->console;
					command.controller = ctrl.at("id");
					command.generation = ctrl.at("generation");
					command.text = target;
					const auto session = owner_.lock();
					if (session && session->console_command(route_, command))
						return true;
				}
	return false;
}
bool ConsoleModel::away_action(const std::string &action, const std::string &mission, const std::string &text)
{
	if (!observation_)
		return false;
	for (const auto &m : away_missions())
		if (m.at("id") == mission)
			for (const auto &ctrl : observation_->gameplay.value("controllers", Json::array()))
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
					if (session && session->console_command(route_, c))
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
			for (const auto &ctrl : observation_->gameplay.value("controllers", Json::array()))
				if (ctrl.value("human", false))
				{
					OperatingCommand command{observation_->own.id, "team." + action, t.at("revision"), team};
					command.text = text;
					command.console = observation_->console;
					command.controller = ctrl.at("id");
					command.generation = ctrl.at("generation");
					if (auto session = owner_.lock(); session && session->console_command(route_, command))
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
	return session && session->console_command(route_, command);
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
	for (const auto &section : observation_->gameplay.value("controllers", Json::array()))
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
		return eligible && session->console_command(route_, eligible->command);
	}
	return false;
}
bool ConsoleModel::submit(OperatingCommand command)
{
	auto session = owner_.lock();
	if (!session || !observation_)
		return false;
	command.console = observation_->console;
	const auto eligible = eligible_console_command(*observation_, layout_, session->public_board(), std::move(command));
	if (!eligible)
		return false;
	command = eligible->command;
	std::string projected_label;
	if (!eligible->descriptor.is_null())
	{
		const auto &descriptor = eligible->descriptor;
		projected_label = console_label(console_string(descriptor, "label_key"));
		if (projected_label.empty())
			projected_label = console_label(console_string(descriptor, "label"));
		if (descriptor.value("label_value", false))
			projected_label += " " + std::to_string(command.value);
	}
	if (!session->console_command(route_, command))
	{
		announcement_ = localise("The server refused that control request");
		return false;
	}
	if (command.action == "speed_mm_s")
		speed_ = command.value;
	else if (command.action == "heading_mdeg")
		heading_ = command.value;
	if (command.action == "speed_mm_s" || command.action == "heading_mdeg")
	{
		pending_at_ = session->ticks();
		const auto binding =
		    command.controller.empty() ? command.system : "controller/" + command.controller + "/" + command.channel;
		pending_requests_[{binding, command.action}] = command.value;
		announcement_ = localise("Requested speed") + " " + std::to_string(speed_ / 1000.0) + " " + localise("m/s") +
		                "; " + localise("heading") + " " + std::to_string(heading_ / 1000) + " " + localise("degrees") +
		                ".";
	}
	else
		announcement_ = localise("Request sent") + (projected_label.empty() ? std::string{} : ": " + projected_label);
	return true;
}
std::vector<ConsoleSystemStatus> ConsoleModel::system_statuses() const
{
	std::vector<ConsoleSystemStatus> result;
	if (!observation_ || !observation_->gameplay.is_object() ||
	    observation_->gameplay.value("combat", Json::object()).empty())
		return result;
	for (const auto &system : observation_->systems)
	{
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
	const auto sensors = observation_->gameplay.value("sensors", Json::object());
	for (const auto &contact : observation_->contacts)
		result.emplace_back("game.sensor-contact/" + std::to_string(contact.id), localise("Inspect contact") + " " +
		                                                                             localise(contact.definition) +
		                                                                             " " + std::to_string(contact.id));
	for (const auto &b : observation_->bearing_contacts)
		result.emplace_back("game.sensor-contact/" + std::to_string(b.id), localise("Inspect contact") + " " +
		                                                                       std::to_string(b.id) + "; " +
		                                                                       bearing_readout(b.bearing_mdeg));
	for (const auto &contact : sensors.value("contacts", Json::array()))
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
	const auto combat = observation_->gameplay.value("combat", Json::object());
	for (const auto &status : system_statuses())
		result.emplace_back("game.status/" + status.system, status.semantic_label);
	if (!status_system_.empty())
		result.emplace_back("game.status-close", localise("Close system details"));
	bool weapons = false;
	for (const auto &descriptor : observation_->gameplay.value("controls", Json::array()))
		weapons |= descriptor.value("action", std::string{}) == "weapon_target" ||
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
	for (const auto &team : observation_->gameplay.value("teams", Json::array()))
	{
		if (team.at("reserved").get<bool>() || !team.value("manual", true))
			continue;
		for (const auto &order : team.at("orders"))
			result.emplace_back(
			    "game.team-order/" + team.at("id").get<std::string>() + "/" + order.at("id").get<std::string>(),
			    team.at("id").get<std::string>() + " (" + localise(team.value("condition", "healthy")) +
			        "): " + localise(order.at("label")) +
			        (team.at("id") == selected_team_ && order.at("id") == selected_team_order_ ? " [*]" : ""));
		if (team.at("order") != "")
			result.emplace_back("game.team-cancel/" + team.at("id").get<std::string>(),
			                    localise("Cancel team order") + " " + team.at("id").get<std::string>());
		if (team.at("id") == selected_team_)
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
		if (!observation_->gameplay.value("ftl", Json::array()).empty() &&
		    descriptor.value("action", "") != "system_enabled" && descriptor.value("action", "") != "ftl.depart")
			continue;
		if (descriptor.value("action", std::string{}) == "system_enabled")
		{
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
			for (const auto &controller : observation_->gameplay.value("controllers", Json::array()))
				if (controller.at("id") == descriptor.value("controller", std::string{}))
					for (const auto &[key, channel] : controller.at("commands").items())
						if (key == descriptor.value("channel", std::string{}))
							for (const auto &consumer : channel.at("consumers"))
								for (const auto &team : observation_->gameplay.value("teams", Json::array()))
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
			label += " " + std::to_string(*console_integer(descriptor.at("value")));
		label = descriptor.value("label_prefix", std::string{}) + label;
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
	const auto combat = observation_->gameplay.value("combat", Json::object());
	for (const auto &f : combat.value("faults", Json::array()))
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
	for (const auto &ftl : observation_->gameplay.value("ftl", Json::array()))
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
	const auto sensors = observation_->gameplay.value("sensors", Json::object());
	for (const auto &b : observation_->bearing_contacts)
		if (b.id == static_cast<EntityId>(sensor_contact_))
			result.emplace_back("game.scan-bearing/" + std::to_string(b.id), localise("Contact") + " " +
			                                                                     std::to_string(b.id) + "; " +
			                                                                     bearing_readout(b.bearing_mdeg));
	for (const auto &scanner : sensors.value("scanners", Json::array()))
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
	for (const auto &contact : sensors.value("contacts", Json::array()))
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
	const auto combat = observation_->gameplay.value("combat", Json::object());
	if (!combat.empty())
	{
		if (combat.contains("result"))
			result.emplace_back("combat.outcome", localise(combat.at("result")));
		if (combat.value("destroyed", false))
			result.emplace_back("combat.result", localise("Ship destroyed"));
		for (const auto &c : combat.value("compartments", Json::array()))
			result.emplace_back("compartment." + c.at("id").get<std::string>(),
			                    c.at("deck").get<std::string>() + " / " + c.at("id").get<std::string>() + ": " +
			                        std::to_string(c.at("hp").get<std::int64_t>()) + "/" +
			                        std::to_string(c.at("maximum").get<std::int64_t>()) + " " +
			                        localise(c.at("disabled").get<bool>() ? "Disabled" : "Operational"));
		for (const auto &t : combat.value("teams", Json::array()))
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
			for (const auto &arc : combat.value("arcs", Json::array()))
				result.emplace_back("arc." + arc.at("id").get<std::string>(),
				                    arc.at("id").get<std::string>() + ": " + localise("Fire arc") + " " +
				                        std::to_string(arc.at("arc_mdeg").get<std::int64_t>() / 1000) + "; " +
				                        localise("Bearing") + " " +
				                        std::to_string(arc.at("bearing_mdeg").get<std::int64_t>() / 1000));
	}
	for (const auto &shield : combat.value("shields", Json::array()))
		result.emplace_back("shield." + shield.value("component_id", shield.at("id").get<std::string>()),
		                    shield.at("id").get<std::string>() + ": " + localise("Stored strength") + " " +
		                        std::to_string(shield.at("strength").get<std::int64_t>()) + "/" +
		                        std::to_string(shield.at("capacity").get<std::int64_t>()) + "; " + localise("Warm-up") +
		                        " " + std::to_string(shield.at("warmup").get<std::int64_t>()) + "/" +
		                        std::to_string(shield.at("warmup_ticks").get<std::int64_t>()));
	for (const auto &weapon : combat.value("weapons", Json::array()))
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
	for (const auto &c : observation_->gameplay.value("psychic", Json::object()).value("choirs", Json::array()))
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

	for (const auto &t : observation_->gameplay.value("sensors", Json::object()).value("tracks", Json::array()))
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
