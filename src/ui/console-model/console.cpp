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
import phoenix.content.controllers;
import phoenix.content.ui;

import phoenix.application.preferences;
import phoenix.console_controls;
#include "model_internal.hpp"
namespace phoenix
{
using namespace console_detail;
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
} // namespace phoenix
