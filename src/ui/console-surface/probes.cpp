module;
#include <iostream>
#include <RmlUi/Core.h>
#include <RmlUi/Core/StyleSheetContainer.h>
#include <RmlUi/Core/StreamMemory.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/StringUtilities.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.ui.console_surface;
import phoenix.content.assets;
import phoenix.content.controllers.types;
import phoenix.content.ui;

import phoenix.graphics.assets;
import phoenix.graphics.radar;
import phoenix.graphics.scene;
import phoenix.ui.theme;
import phoenix.ui.engineering_widgets;
import phoenix.ui.information_widgets;
import phoenix.ui.conversation_panel;
import phoenix.ui.surface_host;
import phoenix.ui.navigation_panel;
import phoenix.ui.operations_widgets;
import phoenix.ui.tactical_widgets;
import phoenix.ui.flight_widgets;
import phoenix.application.preferences;
import phoenix.devices;
#include "surface_state.hpp"
namespace phoenix
{
bool ConsoleSurface::State::probe_throttle(unsigned stage)
{
	if (!model_.layout() || throttle_tracks_.empty())
		return false;
	const auto key = throttle_tracks_.begin()->first;
	const auto track = throttle_tracks_.at(key);
	auto touch = [&](double fraction, DeviceEventKind kind)
	{
		return pointer(89001, track.x + track.width / 2, track.y + track.height * (1 - fraction), kind);
	};
	if (stage == 0)
	{
		if (!touch(.73, DeviceEventKind::Down))
			return false;
		throttle_sent_at_[key] = GetTime();
		return touch(.81, DeviceEventKind::Move) && std::abs(throttles_.fraction(model_, key) - .81) < .0001;
	}
	if (stage == 1)
		return std::abs(throttles_.fraction(model_, key) - .81) < .0001 && throttle_contacts_.contains(89001);
	if (stage == 2)
	{
		const auto zero = throttles_.zero(model_, key);
		if (!touch(std::min(1.0, zero + .02), DeviceEventKind::Move) || throttles_.request(model_, key) != 0)
			return false;
		if (!touch(std::max(0.0, zero - .02), DeviceEventKind::Move) || throttles_.request(model_, key) != 0)
			return false;
		return touch(zero, DeviceEventKind::Up) && throttles_.request(model_, key) == 0;
	}
	if (stage == 3)
	{
		if (!touch(.75, DeviceEventKind::Down) || !touch(.75, DeviceEventKind::Up) ||
		    throttles_.request(model_, key) == 0)
			return false;
		return probe_tab("all-stop/" + key) && throttles_.request(model_, key) == 0;
	}
	return model_.system_request(model_.widget_system(key)) == 0;
}
bool ConsoleSurface::State::probe_conversation() const
{
	auto *history = document_->GetElementById("comms-history");
	if (!history || !history->IsVisible(true) || !history->GetNumChildren())
		return false;
	for (const auto &placement : authored_resolved_.placements)
		if (placement.visible && !placement.component.starts_with("conversation-") &&
		    placement.component != "conversations")
			return false;
	const float density = context_->GetDensityIndependentPixelRatio();
	auto *choices = document_->GetElementById("comms-choices");
	if (history->GetClientHeight() <= choices->GetClientHeight() * 4)
		return false;
	Rml::ElementList replies;
	choices->GetElementsByTagName(replies, "button");
	for (auto *reply : replies)
		if (reply->GetOffsetWidth() < history->GetClientWidth() - 24 * density)
		{
			std::cout << "Comms reply width " << reply->GetOffsetWidth() << "; history " << history->GetClientWidth()
			          << std::endl;
			return false;
		}
	for (int i = 0; i < history->GetNumChildren(); ++i)
	{
		auto *row = history->GetChild(i);
		Rml::ElementList text;
		row->GetElementsByClassName(text, "message-text");
		if (text.empty() || row->GetOffsetWidth() < history->GetClientWidth() - 24 * density)
		{
			std::cout << "Comms row width " << row->GetOffsetWidth() << "; history " << history->GetClientWidth()
			          << std::endl;
			return false;
		}
		if (text.front()->GetOffsetWidth() < row->GetOffsetWidth() - 120 * density)
		{
			std::cout << "Comms text width " << text.front()->GetOffsetWidth() << "; row " << row->GetOffsetWidth()
			          << std::endl;
			return false;
		}
	}
	return true;
}
bool ConsoleSurface::State::probe_tab(const std::string &action)
{
	auto *control = document_->GetElementById(id(action));
	if (!control || !control->IsVisible(true))
		return false;
	Rml::ScrollIntoViewOptions scroll;
	scroll.behavior = Rml::ScrollBehavior::Instant;
	scroll.vertical = Rml::ScrollAlignment::Center;
	scroll.parentage = Rml::ScrollParentage::Closest;
	control->ScrollIntoView(scroll);
	context_->Update();
	const auto at = control->GetAbsoluteOffset(Rml::BoxArea::Border);
	const float x = at.x + control->GetOffsetWidth() / 2, y = at.y + control->GetOffsetHeight() / 2;
	if (pointer(9999, x, y, DeviceEventKind::Down))
	{
		pointer(9999, x, y, DeviceEventKind::Cancel);
		return false;
	}
	auto *hit = context_->GetElementAtPoint({x, y});
	const auto hit_description = hit ? hit->GetTagName() + "#" + hit->GetId() : std::string("none");
	while (hit && hit != control)
		hit = hit->GetParentNode();
	if (!hit)
	{
		std::cout << "Pointer probe missed " << action << " at " << x << "," << y << " hit " << hit_description
		          << std::endl;
		return false;
	}
	const auto previous_power =
	    action.starts_with("power/") && power_controls_.contains(action) ? power_controls_.at(action).value : -1;
	context_->ProcessMouseMove(static_cast<int>(x), static_cast<int>(y), 0);
	context_->ProcessMouseButtonDown(0, 0);
	context_->ProcessMouseButtonUp(0, 0);
	model_.refresh(app_->session, route_);
	if (action.starts_with("power/"))
	{
		const auto pending = power_pending_.find(action);
		return pending != power_pending_.end() && pending->second.value != previous_power;
	}
	if (action.starts_with("game.console/"))
		return model_.observation() && model_.observation()->console == action.substr(13);
	if (action.starts_with("support-page/"))
		return support_primary() && support_page_ == action.substr(13);
	if (action.starts_with("console-page/"))
		for (const auto &page : station_pages_)
			if (page.action == action)
				return !settings_open_ && model_.observation() && model_.observation()->console == page.console &&
				       (page.group.empty() || authored_pages_.at(page.group) == page.page);
	return true;
}
bool ConsoleSurface::State::probe_navigation(unsigned stage)
{
	if (!navigation_.visible())
		return false;
	const auto before = model_.waypoint();
	if (stage == 0)
	{
		const auto x = navigation_.area().x + navigation_.area().width * .12f,
		           y = navigation_.area().y + navigation_.area().height * .12f;
		pointer(9888, x, y, DeviceEventKind::Down);
		pointer(9888, x, y, DeviceEventKind::Up);
		return navigation_.selected() && model_.waypoint() == before;
	}
	const auto previous = navigation_.selected_x();
	document_->GetElementById(id("nav.map"))->Focus();
	context_->ProcessKeyDown(Rml::Input::KI_RIGHT, 0);
	context_->ProcessKeyUp(Rml::Input::KI_RIGHT, 0);
	return navigation_.selected() && navigation_.selected_x() != previous && model_.waypoint() == before;
}
bool ConsoleSurface::State::probe_fit() const
{
	const auto dimensions = context_->GetDimensions();
	for (const auto &placement : authored_resolved_.placements)
		if (placement.visible)
			if (auto *panel = const_cast<State *>(this)->authored_component(placement.component))
			{
				if (!placement.component.starts_with("conversation-") && placement.component != "conversations" &&
				    (panel->GetScrollHeight() > panel->GetClientHeight() + 2 ||
				     panel->GetScrollWidth() > panel->GetClientWidth() + 2))
				{
					std::cerr << "Station component overflows: " << placement.component << " "
					          << panel->GetClientWidth() << "x" << panel->GetClientHeight() << " scroll "
					          << panel->GetScrollWidth() << "x" << panel->GetScrollHeight() << std::endl;
					return false;
				}
			}
	for (const auto &placement : authored_resolved_.placements)
		if (placement.visible && model_.layout())
			for (const auto &widget : model_.layout()->at("widgets"))
				if (widget.at("id") == placement.component && widget.at("kind") == "radar")
				{
					auto *element = document_->GetElementById("widget/" + placement.component);
					const auto at = element->GetAbsoluteOffset(Rml::BoxArea::Border);
					if (at.x < -1 || at.y < -1 || at.x + element->GetOffsetWidth() > dimensions.x + 1 ||
					    at.y + element->GetOffsetHeight() > dimensions.y + 1)
						return false;
				}
	return true;
}
bool ConsoleSurface::State::probe_support_visuals()
{
	auto *diagram = document_->GetElementById(id("shield-diagram"));
	auto *slider = document_->GetElementById(id("support-condition"));
	Rml::ElementList bars;
	if (slider)
		for (int i = 0; i < slider->GetNumChildren(true); ++i)
			if (auto *child = slider->GetChild(i); child->GetTagName() == "sliderbar")
				bars.push_back(child);
	const bool valid = diagram && slider && diagram->IsVisible(true) && slider->IsVisible(true) &&
	                   diagram->GetClientHeight() >= 80 * context_->GetDensityIndependentPixelRatio() &&
	                   shield_ship_sprite_ && !shield_sectors_.empty() && !bars.empty() &&
	                   bars.front()->GetOffsetWidth() > 8 && bars.front()->GetOffsetHeight() > 8;
	if (!valid)
		std::cout << "Support visual probe: canvas " << (diagram ? diagram->GetClientHeight() : -1) << " / density "
		          << context_->GetDensityIndependentPixelRatio() << "; image " << bool(shield_ship_sprite_)
		          << "; sectors " << shield_sectors_.size() << "; thumb "
		          << (bars.empty() ? -1 : bars.front()->GetOffsetWidth()) << ","
		          << (bars.empty() ? -1 : bars.front()->GetOffsetHeight()) << std::endl;
	return valid;
}
bool ConsoleSurface::State::probe_readout_scroll()
{
	// Explicit graphical smoke: deferred text becomes current on scrolling,
	// while its semantic value was already current before it became visible.
	auto values = readout_values_;
	values.insert(values.end(), resource_readouts_.begin(), resource_readouts_.end());
	for (const auto &[name, text] : values)
		if (auto *target = dom_.element(id(name)); target && target->IsVisible(true) && displayed_values_[name] != text)
		{
			const auto semantic = std::find_if(app_->screen_readouts.begin(), app_->screen_readouts.end(),
			                                   [&](const auto &entry)
			                                   {
				                                   return entry.first == id(name) && entry.second.ends_with(text);
			                                   });
			if (semantic == app_->screen_readouts.end())
			{
				std::cerr << "Deferred readout semantic missing: " << name << std::endl;
				return false;
			}
			std::vector<std::pair<Rml::Element *, float>> scroll;
			for (auto *parent = target->GetParentNode(); parent; parent = parent->GetParentNode())
				scroll.emplace_back(parent, parent->GetScrollTop());
			target->ScrollIntoView({Rml::ScrollAlignment::Start, Rml::ScrollAlignment::Nearest,
			                        Rml::ScrollBehavior::Instant, Rml::ScrollParentage::Closest});
			update_view();
			if (displayed_values_[name] != text)
			{
				std::cerr << "Deferred readout did not refresh: " << name << std::endl;
				return false;
			}
			for (const auto &[parent, previous] : scroll)
				parent->SetScrollTop(previous);
			update_view();
			std::cout << "Deferred readout scroll and current semantics passed" << std::endl;
			return true;
		}
	return false;
}
bool ConsoleSurface::State::probe_report()
{
	for (const auto &[name, text] : readout_values_)
		if (text.find("Engine Console report") != std::string::npos)
		{
			auto *target = dom_.element(id(name));
			if (!target)
				return false;
			target->ScrollIntoView();
			context_->Update();
			return true;
		}
	return false;
}
bool ConsoleSurface::State::probe_ready()
{
	auto *control = document_->GetElementById(id("ready"));
	if (!control || !app_)
		return false;
	auto ready = [&]()
	{
		for (const auto &member : app_->session->lobby().members)
			if (member.local_screen == route_)
				return member.ready;
		return false;
	};
	const bool original = ready();
	const auto at = control->GetAbsoluteOffset(Rml::BoxArea::Border);
	context_->ProcessMouseMove(static_cast<int>(at.x + control->GetOffsetWidth() / 2),
	                           static_cast<int>(at.y + control->GetOffsetHeight() / 2), 0);
	for (int frame = 0; frame < 5; ++frame)
	{
		context_->ProcessMouseWheel(0, 0);
		context_->ProcessMouseWheel(1, 0);
		context_->Update();
		if (ready() != original)
			return false;
	}
	context_->ProcessMouseButtonDown(0, 0);
	context_->ProcessMouseButtonUp(0, 0);
	if (ready() == original)
		return false;
	context_->ProcessMouseWheel(0, 0);
	context_->Update();
	if (ready() == original)
		return false;
	context_->ProcessMouseButtonDown(0, 0);
	context_->ProcessMouseButtonUp(0, 0);
	return ready() == original;
}
bool ConsoleSurface::State::probe_on_screen(const std::string &widget)
{
	auto *control = document_->GetElementById(id("on-screen/" + widget));
	if (!control)
		return false;
	const auto at = control->GetAbsoluteOffset(Rml::BoxArea::Border);
	const float x = at.x + control->GetOffsetWidth() / 2, y = at.y + control->GetOffsetHeight() / 2;
	// Exercise the same custom-dial interception and RmlUi hit testing as a pointer click.
	if (sharing_pods_.contains(widget))
	{
		if (heading_dials_.contains(widget))
		{
			const auto [cx, cy, radius] = heading_dials_.at(widget);
			for (int degrees = 0; degrees < 360; ++degrees)
			{
				const float angle = degrees * 3.141592653589793f / 180;
				if (sharing_hit(widget, cx + std::cos(angle) * radius * .97f, cy + std::sin(angle) * radius * .97f))
					return false;
			}
		}
		if (pointer(999998, at.x + 1, at.y + 1, DeviceEventKind::Down))
			return false;
		if (!pointer(999999, x, y, DeviceEventKind::Down) || !pointer(999999, at.x + 1, at.y + 1, DeviceEventKind::Up))
			return false;
		return pointer(999999, x, y, DeviceEventKind::Down) && pointer(999999, x, y, DeviceEventKind::Up);
	}
	context_->ProcessMouseMove(static_cast<int>(x), static_cast<int>(y), 0);
	context_->ProcessMouseButtonDown(0, 0);
	if (pointer(999999, x, y, DeviceEventKind::Up))
		return false;
	context_->ProcessMouseButtonUp(0, 0);
	return true;
}
bool ConsoleSurface::State::probe_helm(bool verify)
{
	if (!heading_dial_ || !model_.layout())
		return false;
	unsigned throttle = 0;
	for (const auto &widget : model_.layout()->at("widgets"))
		if (widget.at("kind") == "throttle")
		{
			const auto binding = model_.widget_system(widget.at("id"));
			const auto track = throttle_tracks_.at(widget.at("id").get<std::string>());
			const float fraction = (++throttle % 2) ? .75f : .25f;
			const auto expected =
			    std::llround(model_.system_minimum(binding) +
			                 (model_.system_maximum(binding) - model_.system_minimum(binding)) * fraction);
			if (!verify)
			{
				pointer(998, track.x + track.width / 2, track.y + track.height * (1 - fraction), DeviceEventKind::Down);
				pointer(998, track.x + track.width / 2, track.y + track.height * (1 - fraction), DeviceEventKind::Up);
			}
			if (std::abs(model_.system_request(binding) - expected) > 1)
				return false;
			if (verify && widget.contains("controller"))
			{
				bool found = false;
				for (const auto &controller : model_.observation()->gameplay.at("controllers"))
					if (controller.at("id") == widget.at("controller"))
					{
						const auto &command = controller.at("commands").at(widget.at("channel").get<std::string>());
						if (std::abs(command.at("value").get<std::int64_t>() - expected) > 1 ||
						    command.at("revision") != command.at("consumed"))
							return false;
						for (const auto &consumer : command.at("consumers"))
						{
							bool physical = false;
							for (const auto &system : model_.observation()->systems)
								if (system.id == consumer.get<std::string>())
									physical = std::abs(system.requested - expected) <= 1;
							if (!physical)
								return false;
						}
						found = true;
					}
				if (!found)
					return false;
			}
		}
	if (verify)
		return throttle > 0;
	const auto [x, y, r] = *heading_dial_;
	return throttle && pointer(999, x, y - r * .85f, DeviceEventKind::Down) &&
	       pointer(999, x, y - r * .85f, DeviceEventKind::Up) && model_.requested_heading() == 90000;
}
bool ConsoleSurface::State::probe_manual(unsigned stage)
{
	if (!model_.manual_available())
		return false;
	if (model_.manual_active() && model_.layout())
		for (const auto &widget : model_.layout()->at("widgets"))
			if (widget.at("kind") == "throttle")
			{
				if (all_stop(widget.at("id")))
					return false;
				if (auto *stop = document_->GetElementById(id("all-stop/" + widget.at("id").get<std::string>()));
				    stop && stop->IsVisible(true))
					return false;
			}

	const auto requested = [&](const std::string &field, int expected)
	{
		for (const auto &system : model_.observation()->systems)
			if (system.telemetry.contains(field) && system.telemetry.at(field) == expected)
				return true;
		std::cout << "Manual probe missing " << field << "=" << expected << "; input " << manual_input_.axes().yaw
		          << "," << manual_input_.axes().thrust << std::endl;
		for (const auto &system : model_.observation()->systems)
			if (system.telemetry.contains(field))
				std::cout << system.id << " actual request " << system.telemetry.at(field) << std::endl;
		return false;
	};
	const auto cx = joystick_area_.x + joystick_area_.width / 2;
	const auto cy = joystick_area_.y + joystick_area_.height / 2;
	if (stage == 0)
		return action(id("manual-mode"));
	if (!model_.manual_active() || joystick_area_.width <= 0)
		return false;
	if (stage == 1)
	{
		if (!pointer(9001, cx - joystick_area_.width / 4, cy - joystick_area_.height / 4, DeviceEventKind::Down) ||
		    !pointer(9001, cx - joystick_area_.width * .375f, cy - joystick_area_.height * .375f,
		             DeviceEventKind::Move))
			return false;
		manual_frame(false, {}, true);
		if (manual_sent_ != FlightAxes{500, 500})
			return false;
		return pointer(9001, cx - joystick_area_.width / 4, cy - joystick_area_.height / 4, DeviceEventKind::Move);
	}
	if (stage == 2)
		return requested("longitudinal_thrust", 500) && requested("manual_steering", 500) &&
		       pointer(9001, -100, -100, DeviceEventKind::Up);
	if (stage == 3)
	{
		if (!requested("longitudinal_thrust", 0) || !requested("manual_steering", 0))
			return false;
		manual_frame(true, {-1000, -1000}, true);
		return true;
	}
	if (stage == 4)
	{
		if (!requested("longitudinal_thrust", -1000) || !requested("manual_steering", -1000))
			return false;
		manual_frame(false, {}, true);
		return true;
	}
	if (stage == 5)
	{
		gamepad("smoke-pad", "flight.yaw", .2f);
		gamepad("smoke-pad", "flight.thrust", .6f);
		manual_frame(false, {}, true);
		return true;
	}
	if (stage == 6)
	{
		if (!requested("longitudinal_thrust", 600) || !requested("manual_steering", 200))
			return false;
		gamepad("smoke-pad", "flight.yaw", 0);
		gamepad("smoke-pad", "flight.thrust", 0);
		manual_frame(false, {}, true);
		return true;
	}
	return requested("longitudinal_thrust", 0) && requested("manual_steering", 0);
}
bool ConsoleSurface::State::probe_engineering(unsigned stage)
{
	std::cout << "Engineering probe stage " << stage << std::endl;
	if (!model_.observation())
		return false;
	const auto cards = engineering_cards(*model_.observation());
	if (cards.empty())
		return false;
	// Actions refresh the model; retain a value snapshot while exercising descriptors.
	const auto view = *model_.observation();
	if (stage == 0)
	{
		auto *panel = document_->GetElementById("engineering-cards");
		std::cout << "Engineering geometry: panel " << panel->GetOffsetWidth() << ", card "
		          << panel->GetChild(0)->GetOffsetWidth() << ", cards " << panel->GetNumChildren() << std::endl;
		if (panel->GetOffsetWidth() <= 0 || panel->GetChild(0)->GetOffsetWidth() > panel->GetOffsetWidth())
			return false;
		bool heat = false, battery = false;
		for (const auto &card : cards)
		{
			heat |= card.heat.has_value();
			battery |= card.battery.has_value();
		}
		const auto descriptors = view.gameplay.at("controls");
		bool profile_selected = false;
		for (const auto &d : descriptors)
			if (d.value("id", "") == "profile.detailed")
			{
				action(id("settings"));
				local_settings_tab_ = "session";
				const auto size = context_->GetDimensions();
				prepare_local_settings(*app_, {route_, 0, 0, static_cast<float>(size.x), static_cast<float>(size.y)});
				profile_selected = action(id(descriptor_action(d.at("id"))));
				action(id("local-settings.close"));
				std::cout << "Engineering detailed profile admission: " << profile_selected << "; "
				          << model_.announcement() << std::endl;
			}
		std::cout << "Engineering telemetry: heat=" << heat << " battery=" << battery
		          << "; profile=" << view.gameplay.value("profile", "missing") << std::endl;
		return heat && battery && profile_selected;
	}
	if (stage == 1)
	{
		std::cout << "Engineering profile: " << view.gameplay.value("profile", "missing") << std::endl;
		std::set<std::string> exercised;
		const auto descriptors = view.gameplay.at("controls");
		for (const auto &d : descriptors)
		{
			const auto operation = d.value("action", "");
			if ((operation == "generator_output" || operation == "group_allocation" ||
			     operation == "cooling_allocation") &&
			    d.value("value", std::int64_t{0}) > 0 && !exercised.contains(operation))
			{
				std::cout << "Engineering command: " << d.at("id") << std::endl;
				if (!action(id(descriptor_action(d.at("id")))))
					return false;
				exercised.insert(operation);
				const auto consumers = command_consumers(view, d);
				if (consumers.empty())
					return false;
				for (const auto &consumer : consumers)
					engineering_probe_requests_[{consumer, operation}] = d.at("value");
			}
		}
		std::cout << "Engineering operation families exercised: " << exercised.size() << std::endl;
		return exercised.size() == 3;
	}
	for (const auto &[key, value] : engineering_probe_requests_)
	{
		const auto system = std::find_if(view.systems.begin(), view.systems.end(),
		                                 [&](const auto &s)
		                                 {
			                                 return s.id == key.first;
		                                 });
		if (system == view.systems.end() || !system->telemetry.contains(key.second) ||
		    system->telemetry.at(key.second) != value)
			return false;
	}
	return !action(id("release")) && !action(id("local-settings.quit"));
}
} // namespace phoenix
