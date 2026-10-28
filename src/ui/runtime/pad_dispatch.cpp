module;
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <raylib.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
module phoenix.ui.pad_dispatch;

import phoenix.navigation;
import phoenix.graphics.radar;
import phoenix.platform.accessibility;
namespace phoenix
{
struct PadDispatcher::State
{
	InputRouter &input;
	const std::map<ConsoleId, std::unique_ptr<ConsoleSurface>> &console_surfaces;
	Rml::Context *&context;
	Rml::ElementDocument *&document;
	const std::vector<ConsoleRegion> &displayed_regions;
	std::function<bool()> window_focused;

public:
	State(InputRouter &router, const std::map<ConsoleId, std::unique_ptr<ConsoleSurface>> &surfaces,
	      Rml::Context *&navigation_context, Rml::ElementDocument *&navigation_document,
	      const std::vector<ConsoleRegion> &regions, std::function<bool()> focused);

	struct PadSurface
	{
		PadNavigation navigation;
		std::string device, identity;
		ConsoleId route = 0;
		bool ui = true, flight = false;
		Screen screen = Screen::home;
		std::weak_ptr<SessionPort> session;
		std::uint64_t generation = 0;
	};
	struct PadSelection
	{
		std::string selected, held, holder, editing;
		double pending = 0;
	};
	std::map<std::string, PadSurface> pad_surfaces;
	std::map<ConsoleId, PadSelection> pad_selections;
	Rml::Element *pad_element(ConsoleId route, const std::string &id);

	void release_pad(ConsoleId route);

	void cancel_pad(const std::string &device);

	void prepare_pad(const std::string &device, ConsoleId route, const Application &app);

	std::vector<SemanticControl> pad_controls(const Application &app, ConsoleId route, const std::string &device);

	void pad_select(PadSelection &selection, ConsoleId route, const SemanticControl &control, Application &app);

	void pad_action(Application &app, PadSurface &pad, const PadNavigationEvent &event);

	void update_pads(Application &app);

	void draw_pad_selection(Application &app);

	bool probe(Application &app, unsigned stage);
};
} // namespace phoenix

namespace phoenix
{
PadDispatcher::State::State(InputRouter &router, const std::map<ConsoleId, std::unique_ptr<ConsoleSurface>> &surfaces,
                            Rml::Context *&navigation_context, Rml::ElementDocument *&navigation_document,
                            const std::vector<ConsoleRegion> &regions, std::function<bool()> focused)
    : input(router), console_surfaces(surfaces), context(navigation_context), document(navigation_document),
      displayed_regions(regions), window_focused(std::move(focused))
{
}
Rml::Element *PadDispatcher::State::pad_element(ConsoleId route, const std::string &id)
{
	if (!route && id.starts_with("console/"))
	{
		const auto end = id.find('/', 8);
		if (end != std::string::npos)
			route = std::stoull(id.substr(8, end - 8));
	}
	if (route && console_surfaces.contains(route))
		return console_surfaces.at(route)->controller_element(id);
	return document->GetElementById(id);
}
void PadDispatcher::State::release_pad(ConsoleId route)
{
	auto &selection = pad_selections[route];
	if (auto *element = pad_element(route, selection.selected))
		element->SetClass("pad-selected", false);
	if (console_surfaces.contains(route))
	{
		if (!selection.held.empty())
			console_surfaces.at(route)->controller_hold(selection.held, false);
		console_surfaces.at(route)->controller_cancel();
	}
	selection.held.clear();
	selection.holder.clear();
	selection.editing.clear();
}
void PadDispatcher::State::cancel_pad(const std::string &device)
{
	if (auto it = pad_surfaces.find(device); it != pad_surfaces.end())
	{
		release_pad(it->second.route);
		it->second.navigation.cancel();
		pad_surfaces.erase(it);
	}
}
void PadDispatcher::State::prepare_pad(const std::string &device, ConsoleId route, const Application &app)
{
	auto &pad = pad_surfaces[device];
	pad.device = device;
	const bool flight = console_surfaces.contains(route) && console_surfaces.at(route)->controller_flight() &&
	                    (route != 0 && route != 31 || app.screen != Screen::settings);
	const auto identity =
	    console_surfaces.contains(route) ? console_surfaces.at(route)->controller_identity() : std::string{};
	const auto generation = app.session ? app.session->generation() : 0;
	const auto screen = route && route != 31 && app.screen == Screen::settings ? app.settings_return : app.screen;
	if (pad.route != route || pad.screen != screen || pad.session.lock() != app.session ||
	    pad.generation != generation || pad.flight != flight || pad.identity != identity)
	{
		release_pad(pad.route);
		input.assign_gamepad(device, {});
		pad.navigation.cancel();
		pad.route = route;
		pad.screen = screen;
		pad.session = app.session;
		pad.generation = generation;
		pad.flight = flight;
		pad.identity = identity;
		pad.ui = !flight;
		pad_selections[route].selected.clear();
	}
}
std::vector<SemanticControl> PadDispatcher::State::pad_controls(const Application &app, ConsoleId route,
                                                                const std::string &device)
{
	auto controls = navigation_controls(app);
	const auto prefix = "console/" + std::to_string(route) + "/";
	std::erase_if(controls,
	              [&](const auto &control)
	              {
		              if (control.read_only || !control.placement.enabled ||
		                  (route ? !control.id.starts_with(prefix)
		                         : (control.id.starts_with("console/") && !control.id.ends_with("/pad/" + device))))
			              return true;
		              auto *element = pad_element(route, control.id);
		              const auto *field = dynamic_cast<Rml::ElementFormControl *>(element);
		              return !element || !element->IsVisible(true) || (field && field->IsDisabled());
	              });
	const auto order = [&](const SemanticControl &control)
	{
		std::vector<std::uint64_t> path;
		for (auto *node = pad_element(route, control.id); node && node->GetParentNode(); node = node->GetParentNode())
		{
			auto *parent = node->GetParentNode();
			for (int i = 0; i < parent->GetNumChildren(); ++i)
				if (parent->GetChild(i) == node)
				{
					path.push_back(i);
					break;
				}
		}
		std::reverse(path.begin(), path.end());
		const auto document_route =
		    control.id.starts_with("console/") ? std::stoull(control.id.substr(8, control.id.find('/', 8) - 8)) : 0;
		path.insert(path.begin(), document_route);
		return path;
	};
	std::map<std::string, std::vector<std::uint64_t>> orders;
	for (const auto &control : controls)
		orders.emplace(control.id, order(control));
	std::stable_sort(controls.begin(), controls.end(),
	                 [&](const auto &a, const auto &b)
	                 {
		                 return orders.at(a.id) < orders.at(b.id);
	                 });
	return controls;
}
void PadDispatcher::State::pad_select(PadSelection &selection, ConsoleId route, const SemanticControl &control,
                                      Application &app)
{
	if (auto *old = pad_element(route, selection.selected))
		old->SetClass("pad-selected", false);
	selection.selected = control.id;
	if (auto *element = pad_element(route, control.id))
		element->SetClass("pad-selected", true);
	if (auto *element = pad_element(route, control.id))
		element->ScrollIntoView({Rml::ScrollAlignment::Nearest, Rml::ScrollAlignment::Nearest,
		                         Rml::ScrollBehavior::Instant, Rml::ScrollParentage::Closest});
	app.accessible_status = control.label;
}
void PadDispatcher::State::pad_action(Application &app, PadSurface &pad, const PadNavigationEvent &event)
{
	auto &selection = pad_selections[pad.route];
	if (!event.pressed)
	{
		if (event.action == "activate" && !selection.held.empty() && selection.holder == pad.device)
		{
			if (console_surfaces.contains(pad.route))
				console_surfaces.at(pad.route)->controller_hold(selection.held, false);
			selection.held.clear();
		}
		return;
	}
	if (event.action == "mode" && pad.flight)
	{
		release_pad(pad.route);
		pad.ui = !pad.ui;
		pad.navigation.cancel();
		for (const auto &[device, candidate] : pad_surfaces)
			if (&candidate == &pad)
				input.assign_gamepad(device, {});
		app.accessible_status = localise(pad.ui ? "Controller: UI navigation" : "Controller: Flight");
		return;
	}
	if (!pad.ui)
		return;
	if (event.action == "settings")
	{
		release_pad(pad.route);
		invoke_navigation(app, pad.route ? "console/" + std::to_string(pad.route) + "/settings" : "settings");
		return;
	}
	if (event.action == "cancel")
	{
		if (!selection.editing.empty())
		{
			selection.editing.clear();
			return;
		}
		release_pad(pad.route);
		if (console_surfaces.contains(pad.route) && console_surfaces.at(pad.route)->controller_back())
			return;
		if (app.screen == Screen::settings)
		{
			invoke_navigation(app, "settings.close");
			return;
		}
		for (const auto &control : navigation_controls(app))
			if (control.id == "menu.back")
			{
				invoke_navigation(app, "menu.back");
				return;
			}
		context->ProcessKeyDown(Rml::Input::KI_ESCAPE, 0);
		context->ProcessKeyUp(Rml::Input::KI_ESCAPE, 0);
		return;
	}
	auto controls = pad_controls(app, pad.route, pad.device);
	if (controls.empty())
	{
		release_pad(pad.route);
		selection.selected.clear();
		return;
	}
	auto current = std::find_if(controls.begin(), controls.end(),
	                            [&](const auto &c)
	                            {
		                            return c.id == selection.selected;
	                            });
	if (current == controls.end())
	{
		release_pad(pad.route);
		pad_select(selection, pad.route, controls.front(), app);
		current = controls.begin();
	}
	if (!selection.editing.empty() && selection.editing != current->id)
		selection.editing.clear();
	if (event.action == "activate")
	{
		if (!selection.held.empty())
			return;
		if (current->editable)
		{
			if (current->numeric)
			{
				if (selection.editing == current->id)
				{
					if (app.set_control_value)
						app.set_control_value(current->id,
						                      std::to_string(static_cast<long long>(std::llround(selection.pending))));
					selection.editing.clear();
				}
				else
				{
					selection.editing = current->id;
					try
					{
						selection.pending = std::stod(current->value);
					}
					catch (...)
					{
						selection.pending = 0;
					}
				}
			}
			else if (app.focus_control)
			{
				app.focus_control(current->id);
				focus_semantic_text(current->id);
				app.accessible_status = localise("Use the keyboard to enter text");
			}
		}
		else if (console_surfaces.contains(pad.route) &&
		         console_surfaces.at(pad.route)->controller_hold(current->id, true))
		{
			selection.held = current->id;
			selection.holder = pad.device;
		}
		else
			invoke_navigation(app, current->id);
		return;
	}
	const int dx = event.action == "left" ? -1 : event.action == "right" ? 1 : 0;
	const int dy = event.action == "up" ? -1 : event.action == "down" ? 1 : 0;
	if (!selection.editing.empty() && (dx || dy))
	{
		selection.pending =
		    std::clamp(selection.pending + (dx - dy) * current->step, current->minimum, current->maximum);
		app.accessible_status = current->label + ": " + std::to_string(selection.pending);
		return;
	}
	if (event.action == "previous" || event.action == "next")
	{
		const auto index =
		    (current - controls.begin() + controls.size() + (event.action == "next" ? 1 : -1)) % controls.size();
		pad_select(selection, pad.route, controls[index], app);
	}
	else if (dx || dy)
	{
		const auto &a = current->placement;
		double best = 1e30;
		const SemanticControl *next = nullptr;
		for (const auto &candidate : controls)
		{
			const auto &b = candidate.placement;
			const double x = b.x + b.width / 2 - a.x - a.width / 2, y = b.y + b.height / 2 - a.y - a.height / 2;
			if (x * dx + y * dy <= 1)
				continue;
			const double score = std::hypot(x, y) + 2 * std::abs(dx ? y : x);
			if (score < best)
			{
				best = score;
				next = &candidate;
			}
		}
		if (next)
			pad_select(selection, pad.route, *next, app);
	}
}
void PadDispatcher::State::update_pads(Application &app)
{
	for (auto &[device, pad] : pad_surfaces)
	{
#ifndef __EMSCRIPTEN__
		if (app.gamepad_devices.contains(device) &&
		    (!app.gamepad_assignments.contains(device) || app.gamepad_assignments.at(device) != pad.route))
		{
			release_pad(pad.route);
			input.disconnect_gamepad(device);
			pad.navigation.cancel();
			continue;
		}
#endif

		if (!window_focused() || app.screen == Screen::workshop || app.screen == Screen::gm ||
		    app.gamepad_wait_neutral.contains(device) ||
		    (pad.route &&
		     (!app.gamepad_assignments.contains(device) || app.gamepad_assignments.at(device) != pad.route)))
		{
			release_pad(pad.route);
			pad.navigation.cancel();
			continue;
		}
		for (const auto &event : pad.navigation.drain(GetTime()))
			pad_action(app, pad, event);
		const auto controls = pad_controls(app, pad.route, pad.device);
		auto &selection = pad_selections[pad.route];
		const auto available = [&](const std::string &id)
		{
			return std::any_of(controls.begin(), controls.end(),
			                   [&](const auto &c)
			                   {
				                   return c.id == id;
			                   });
		};
		if ((!selection.held.empty() && !available(selection.held)) ||
		    (!selection.editing.empty() && !available(selection.editing)))
			release_pad(pad.route);
		if (!selection.selected.empty() && !available(selection.selected))
			selection.selected.clear();
	}
}
void PadDispatcher::State::draw_pad_selection(Application &app)
{
	for (const auto &[device, pad] : pad_surfaces)
	{
		if (!window_focused())
			continue;
		float badge_x = 8, badge_y = static_cast<float>(GetScreenHeight()) - 24;
		for (const auto &r : app.console_regions)
			if (r.console == pad.route)
			{
				badge_x = r.x + 8;
				badge_y = r.y + r.height - 24;
			}
		DrawText(localise(pad.ui ? "UI navigation | Select: mode | A: activate | B: back | LB/RB: previous/next"
		                         : "Flight | Select: UI navigation")
		             .c_str(),
		         static_cast<int>(badge_x), static_cast<int>(badge_y), 16, ORANGE);
		const auto &selection = pad_selections[pad.route];
		const auto controls = pad_controls(app, pad.route, pad.device);
		for (const auto &control : controls)
			if (pad.ui && control.id == selection.selected)
				if (auto *element = pad_element(pad.route, control.id))
				{
					const auto offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
					float x = 0, y = 0;
					for (const auto &r : displayed_regions)
						if (r.console == (control.id.starts_with("console/")
						                      ? std::stoull(control.id.substr(8, control.id.find('/', 8) - 8))
						                      : 0))
						{
							x = r.x;
							y = r.y;
						}
					if (element->IsClassSet("radar-contact"))
					{
						const auto c = element->GetProperty<Rml::Colourb>("image-color");
						draw_radar_selection({x + offset.x + element->GetOffsetWidth() / 2,
						                      y + offset.y + element->GetOffsetHeight() / 2},
						                     16, {c.red, c.green, c.blue, c.alpha});
					}
					else
						DrawRectangleLinesEx(
						    {x + offset.x, y + offset.y, element->GetOffsetWidth(), element->GetOffsetHeight()}, 3,
						    ORANGE);
					if (!selection.editing.empty())
						DrawText(std::to_string(selection.pending).c_str(), static_cast<int>(x + offset.x),
						         static_cast<int>(y + offset.y), 20, ORANGE);
				}
	}
}
bool PadDispatcher::State::probe(Application &app, unsigned stage)
{
	std::cout << "GUI navigation stage " << stage << std::endl;
	const ConsoleId route = 1;
	const std::string device = "gui-smoke";
	if (!console_surfaces.contains(route))
		return false;
	app.gamepad_assignments[device] = route;
	prepare_pad(device, route, app);
	auto &pad = pad_surfaces.at(device);
	pad.ui = true;
	auto pulse = [&](const std::string &control)
	{
		pad.navigation.sample("flight.yaw", 0, GetTime());
		pad.navigation.sample("flight.thrust", 0, GetTime());
		pad.navigation.sample(control, 1, GetTime());
		update_pads(app);
		pad.navigation.sample(control, 0, GetTime());
		update_pads(app);
	};
	auto activate = [&](const std::string &suffix)
	{
		const auto controls = pad_controls(app, route, device);
		for (const auto &control : controls)
			if (control.id.ends_with(suffix))
			{
				pad_select(pad_selections[route], route, control, app);
				pulse("game.contacts");
				return true;
			}
		return false;
	};
	if (stage == 0)
	{
		const auto controls = pad_controls(app, route, device);
		if (controls.size() < 3)
			return false;
		const auto keyboard = input.keyboard_console();
		std::set<std::string> reached;
		for (std::size_t i = 0; i < controls.size(); ++i)
		{
			pulse("flight.starboard");
			reached.insert(pad_selections[route].selected);
		}
		if (reached.size() != controls.size() || input.keyboard_console() != keyboard)
			return false;
		if (console_surfaces.contains(2))
		{
			const auto first_selection = pad_selections[route].selected;
			app.gamepad_assignments["gui-second"] = 2;
			prepare_pad("gui-second", 2, app);
			auto &second = pad_surfaces.at("gui-second");
			second.ui = true;
			second.navigation.sample("flight.yaw", 0, GetTime());
			second.navigation.sample("flight.thrust", 0, GetTime());
			second.navigation.sample("flight.starboard", 1, GetTime());
			update_pads(app);
			second.navigation.sample("flight.starboard", 0, GetTime());
			update_pads(app);
			if (pad_selections[2].selected.empty() || pad_selections[route].selected != first_selection ||
			    input.keyboard_console() != keyboard)
				return false;
		}

		for (const auto &control : controls)
			if (control.numeric)
			{
				pad_select(pad_selections[route], route, control, app);
				int commits = 0;
				auto original = app.set_control_value;
				app.set_control_value = [&](const std::string &id, const std::string &value)
				{
					if (id == control.id)
						++commits;
					return original && original(id, value);
				};
				pulse("game.contacts");
				pulse("game.right");
				pulse("game.camera-back");
				const bool cancelled = commits == 0 && pad_selections[route].editing.empty();
				pulse("game.contacts");
				pulse("game.right");
				pulse("game.contacts");
				app.set_control_value = original;
				if (!cancelled || commits != 1)
					return false;
				break;
			}
		return activate("/settings");
	}
	if (stage == 1)
		return activate("/local-settings.tab/display");
	if (stage == 2)
	{
		for (int i = 0; i < 5; ++i)
			if (!activate("/local-settings.larger"))
				return false;
		return true;
	}
	if (stage == 3)
	{
		auto *button = console_surfaces.at(route)->controller_element("console/1/local-settings.larger");
		if (!button || button->GetComputedValues().font_size() < 29.f)
			return false;
		if (console_surfaces.contains(2))
		{
			auto *other = console_surfaces.at(2)->controller_element("console/2/settings");
			if (other && other->GetComputedValues().font_size() >= button->GetComputedValues().font_size())
				return false;
		}

		return activate("/local-settings.close");
	}
	if (stage == 5)
		return activate("/settings");
	if (stage == 6)
		return activate("/local-settings.tab/audio");
	if (stage == 7)
	{
		TakeScreenshot("build/screen-devices-console-audio.png");
		if (!activate("/device.audio/muted") || !activate("/device.audio/default"))
			return false;
		return activate("/local-settings.tab/controllers");
	}
	if (stage == 8)
	{
		TakeScreenshot("build/screen-devices-console-controllers.png");
		return activate("/device.pair") && activate("/local-settings.close");
	}
	if (stage == 4)
	{
		pulse("game.manual");
		const bool flight = !pad.ui;
		pulse("game.manual");
		return flight && pad.ui;
	}
	return false;
}
PadDispatcher::PadDispatcher(InputRouter &router, const std::map<ConsoleId, std::unique_ptr<ConsoleSurface>> &surfaces,
                             Rml::Context *&context, Rml::ElementDocument *&document,
                             const std::vector<ConsoleRegion> &regions, std::function<bool()> focused)
    : state_(std::make_unique<State>(router, surfaces, context, document, regions, std::move(focused)))
{
}
PadDispatcher::~PadDispatcher() = default;
void PadDispatcher::cancel(const std::string &device)
{
	state_->cancel_pad(device);
}
void PadDispatcher::sample(const Application &app, const std::string &device, ConsoleId route,
                           const std::string &control, float value)
{
	state_->prepare_pad(device, route, app);
	auto &pad = state_->pad_surfaces.at(device);
	if (state_->window_focused())
	{
		pad.navigation.sample(control, value, GetTime());
		if (!pad.ui && route)
		{
			state_->input.assign_gamepad(device, route);
			state_->input.gamepad(device, control, value);
		}
	}
}
void PadDispatcher::update(Application &app)
{
	state_->update_pads(app);
}
void PadDispatcher::draw(Application &app)
{
	state_->draw_pad_selection(app);
}
bool PadDispatcher::probe(Application &app, unsigned stage)
{
	return state_->probe(app, stage);
}
void PadDispatcher::reset()
{
	state_->pad_surfaces.clear();
	state_->pad_selections.clear();
}
} // namespace phoenix
