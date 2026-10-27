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
const Json &borrowed_member(const Json &value, const std::string &key, bool array = false)
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
UiAvailability console_ui_availability(const Observation &view)
{
	UiAvailability result;
	result.profile = view.gameplay.value("profile", std::string{});
	const auto stars = view.gameplay.value("profile_stars", Json::object());
	result.stars = stars.value(result.profile, 0);
	const auto controls = view.gameplay.value("controls", Json::array());
	for (const auto &control : controls)
	{
		result.actions.insert(control.value("action", std::string{}));
		const auto controller = control.value("controller", std::string{});
		const auto channel = control.value("channel", std::string{});
		if (!controller.empty() && !channel.empty())
		{
			result.controllers.insert(controller);
			result.bindings.insert("controller/" + controller + "/" + channel);
		}
		const auto system = control.value("system", std::string{});
		if (!system.empty() && std::any_of(view.systems.begin(), view.systems.end(),
		                                   [&](const auto &physical)
		                                   {
			                                   return physical.id == system;
		                                   }))
			result.systems.insert(system);
	}
	// Consumers identify the physical systems behind the permitted command channels.
	for (const auto &controller : view.gameplay.value("controllers", Json::array()))
		for (const auto &[channel, command] : controller.at("commands").items())
			if (result.bindings.contains("controller/" + controller.at("id").get<std::string>() + "/" + channel))
				for (const auto &consumer : command.at("consumers"))
					result.systems.insert(consumer.get<std::string>());
	for (const auto &craft : view.gameplay.value("craft", Json::array()))
		if (craft.value("manual", false))
			result.features.insert("craft");
	if (!view.gameplay.value("docking", Json::object()).value("ports", Json::array()).empty())
		result.features.insert("docking");
	const auto logistics = view.gameplay.value("logistics", Json::object());
	auto human = [&](const std::string &id)
	{
		for (const auto &controller : view.gameplay.value("controllers", Json::array()))
			if (controller.at("id") == id)
				return controller.value("human", false);
		return false;
	};
	if (logistics.value("manual", false))
	{
		bool available = human(logistics.value("controller", "")) || human(logistics.value("route_controller", ""));
		for (const auto &store : logistics.value("stores", Json::array()))
			available |= human(store.value("controller", ""));
		if (available &&
		    (!logistics.value("stores", Json::array()).empty() || !logistics.value("routes", Json::array()).empty()))
			result.features.insert("operations");
	}
	if (view.gameplay.value("personnel", Json::object()).value("manual", false) &&
	    !view.gameplay.value("personnel", Json::object()).value("officers", Json::array()).empty())
		result.features.insert("personnel");
	for (const auto &system : view.systems)
		if (system.kind == OperatingSystem::Dispatch && result.systems.contains(system.id))
			result.features.insert("away");
	for (const auto &mission : view.gameplay.value("away_missions", Json::array()))
		if (mission.value("dispatch", false))
			result.features.insert("away");
	for (const auto &team : view.gameplay.value("teams", Json::array()))
		if (team.value("repair", false) &&
		    !view.gameplay.value("combat", Json::object()).value("compartments", Json::array()).empty())
			result.features.insert("repair");
	for (const auto &controller : view.gameplay.value("controllers", Json::array()))
		for (const auto &[channel, command] : controller.at("commands").items())
			for (const auto &consumer : command.at("consumers"))
				for (const auto &system : view.systems)
					if (system.id == consumer.get<std::string>() &&
					    (system.kind == OperatingSystem::Generator || system.kind == OperatingSystem::Distribution ||
					     system.kind == OperatingSystem::Coolant || system.kind == OperatingSystem::Storage))
						result.features.insert("power");
	for (const auto &control : controls)
	{
		const auto action = control.value("action", "");
		if (action == "camera_mode" || action == "camera_direction")
			result.features.insert("camera");
		if (action == "alert.set")
			result.features.insert("alert");
	}
	for (const auto &system : view.systems)
	{
		if (system.kind == OperatingSystem::Weapon && result.systems.contains(system.id))
			result.features.insert("weapons");
		if (system.kind == OperatingSystem::Shield)
			result.features.insert("shields");
	}
	if (!view.gameplay.value("ftl", Json::array()).empty())
		result.features.insert("ftl");
	if (!view.gameplay.value("teams", Json::array()).empty())
		result.features.insert("teams");
	const auto conversations = view.gameplay.value("conversations", Json::object());
	for (const auto &thread : conversations.value("threads", Json::array()))
		if (const auto selected = conversations.value("selected", std::uint64_t{0});
		    selected && thread.value("id", std::uint64_t{0}) == selected)
		{
			if (!thread.value("incoming", Json::array()).empty())
				result.features.insert("conversation-incoming");
			if (!thread.value("choices", Json::array()).empty())
				result.features.insert("conversation-replies");
			if (thread.value("text_allowed", false))
				result.features.insert("conversation-compose");
		}
	for (const auto &extra : view.additional_consoles)
	{
		const auto granted = console_ui_availability(extra);
		for (const auto &feature : granted.features)
			if (feature == "weapons" || feature == "shields" || feature == "ftl" || feature == "camera" ||
			    feature == "alert")
				result.features.insert(feature);
	}
	return result;
}
void ConsoleModel::reset()
{
	combat_target_ = 0;
	selected_compartment_ = 0;
	selected_team_.clear();
	selected_team_order_.clear();
	selected_patient_.clear();
	granted_tabs_ = UiTabs{};
	resolved_ship_.clear();
	resolved_consoles_.clear();
	browsing_.clear();
	fire_arcs_ = true;
	owner_.reset();
	packs_.clear();
	observation_.reset();
	ui_availability_ = {};
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
	auto next = session->console_snapshot(route);
	available_consoles_.clear();
	if (next)
	{
		if (resolved_ship_ != next->own.definition)
		{
			resolved_consoles_ = resolve_consoles(catalog, next->own.definition);
			resolved_ship_ = next->own.definition;
		}
		available_consoles_.emplace_back(next->console, next->console);
		for (const auto &extra : next->additional_consoles)
			available_consoles_.emplace_back(extra.console, extra.console);
		for (const auto &seat : resolved_consoles_)
			for (auto &[key, label] : available_consoles_)
				if (key == seat.id)
					label = catalog.consoles.at(seat.definition).value("name", key);
		std::erase_if(available_consoles_,
		              [&](const auto &entry)
		              {
			              const Observation *view = next.get();
			              for (const auto &extra : next->additional_consoles)
				              if (extra.console == entry.first)
					              view = &extra;
			              for (const auto &seat : resolved_consoles_)
				              if (seat.id == entry.first)
				              {
					              const auto &definition = catalog.consoles.at(seat.definition);
					              return definition.contains("when") &&
					                     !ui_condition_matches(definition.at("when"), console_ui_availability(*view));
				              }
			              return false;
		              });
		std::vector<UiTab> tabs;
		auto attention = [](const Observation &view)
		{
			const auto board = view.gameplay.value("conversations", Json::object());
			for (const auto &thread : board.value("threads", Json::array()))
				if (thread.value("unread", false))
					return true;
			return false;
		};
		for (const auto &[key, label] : available_consoles_)
		{
			bool unread = key == next->console && attention(*next);
			for (const auto &extra : next->additional_consoles)
				if (extra.console == key)
					unread = attention(extra);
			tabs.push_back({key, label, true, unread});
		}
		granted_tabs_.update(std::move(tabs));
		const auto &selected_console = granted_tabs_.selected();
		if (!selected_console.empty() && selected_console != next->console)
		{
			const auto extra = std::find_if(next->additional_consoles.begin(), next->additional_consoles.end(),
			                                [&](const auto &view)
			                                {
				                                return view.console == selected_console;
			                                });
			if (extra != next->additional_consoles.end())
			{
				next = std::shared_ptr<const Observation>(next, &*extra);
			}
		}
		if (granted_tabs_.selected().empty())
			next.reset();
	}
	const auto availability = next ? console_ui_availability(*next) : UiAvailability{};
	const bool changed = bool(observation_) != bool(next) ||
	                     (next && observation_ &&
	                      (next->own.id != observation_->own.id || next->console != observation_->console ||
	                       next->controls != observation_->controls || next->feeds != observation_->feeds));
	if (!changed && availability != ui_availability_)
	{
		release_controls();
		pending_at_.reset();
		pending_requests_.clear();
		++layout_revision_;
	}
	ui_availability_ = availability;
	if (changed)
	{
		if (observation_)
			release_controls();
		if (observation_)
			browsing_[std::to_string(observation_->own.id) + "/" + observation_->console] = {
			    combat_target_,       selected_compartment_, sensor_contact_, selected_team_,
			    selected_team_order_, selected_patient_,     status_system_};
		const auto key = next ? std::to_string(next->own.id) + "/" + next->console : std::string{};
		const auto saved = browsing_.contains(key) ? browsing_.at(key) : Browsing{};
		combat_target_ = saved.target;
		selected_compartment_ = saved.compartment;
		sensor_contact_ = saved.sensor;
		selected_team_ = saved.team;
		selected_team_order_ = saved.order;
		selected_patient_ = saved.patient;
		status_system_ = saved.status;
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
			for (const auto &seat : resolved_consoles_)
				if (seat.id == next->console || next->console.empty())
				{
					if (catalog.consoles.contains(seat.definition))
					{
						layout_ = catalog.consoles.at(seat.definition);
						if (route == 31 && layout_.contains("compact"))
						{
							layout_["widgets"] = layout_.at("compact").at("widgets");
							if (layout_.contains("hud_layout"))
								layout_["layout"] = layout_.at("hud_layout");
							layout_.erase("layout_overrides");
							if (layout_.contains("hud_layout_overrides"))
								layout_["layout_overrides"] = layout_.at("hud_layout_overrides");
						}
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
		if (observation_)
			for (const auto &control : observation_->gameplay.value("controls", Json::array()))
				if (control.value("shared_scan_target", false) && control.at("value") == sensor_contact_)
					return submit(decode_operating_command(control));
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
		const auto requested = id.substr(13);
		if (std::none_of(available_consoles_.begin(), available_consoles_.end(),
		                 [&](const auto &seat)
		                 {
			                 return seat.first == requested;
		                 }))
			return false;
		release_controls();
		granted_tabs_.select(requested);
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
		if (console_string(*selected, "action") == "stance.set" ||
		    console_string(*selected, "action") == "objective.priority")
		{
			OperatingCommand intent{observation_->own.id, console_string(*selected, "action"), selected->at("value"),
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
	const bool accepted = eligible && session->dispatch_console_command(route_, eligible->command);
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
					       session->dispatch_console_command(route_, command);
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
				if (ctrl.value("human", false))
				{
					OperatingCommand command{observation_->own.id, "team." + action, t.at("revision"), team};
					command.text = text;
					command.console = observation_->console;
					command.controller = ctrl.at("id");
					command.generation = ctrl.at("generation");
					if (auto session = owner_.lock(); session && session->dispatch_console_command(route_, command))
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
		projected_label = console_execution_prefix(descriptor) + projected_label;
	}
	if (!session->dispatch_console_command(route_, command))
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
		auto text = localise(process.at("label")) + "; " + localise(process.at("phase"));
		text += "; " + localise(process.at("requested").get<bool>() ? "Requested" : "Stopped");
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
