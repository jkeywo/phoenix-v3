module;
#include <raylib.h>
#include <RmlUi/Core.h>
#include <stdexcept>
#include <filesystem>
#include <algorithm>
#include <map>
#include <set>
#include <optional>
#include <nlohmann/json.hpp>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// clang-format off: embedded JavaScript must retain its operators.
EM_JS(bool, phoenix_canvas_has_focus, (), { return document.activeElement === Module.canvas; });
EM_JS(bool, phoenix_document_has_focus, (), { return document.hasFocus(); });
EM_JS(int, phoenix_canvas_width, (), { return Math.max(1,Module.canvas.clientWidth); });
EM_JS(int, phoenix_canvas_height, (), { return Math.max(1,Module.canvas.clientHeight); });
// clang-format on
#endif
#include <functional>
#include <iostream>
#include <memory>
#include <rlgl.h>
#include <string>
#include <utility>
#include <vector>

module phoenix.presentation;
import phoenix.application;
import phoenix.navigation;
import phoenix.console;
import phoenix.devices;
#include "rml_backend.hpp"
#include "game.hpp"
#include "consoles.hpp"

namespace phoenix
{
namespace
{
UiSystem system;
UiRenderer renderer;
Rml::Context *context = nullptr;
Rml::ElementDocument *document = nullptr;
Application *current_app = nullptr;
std::unique_ptr<GameScreen> game_screen;
std::map<ConsoleId, std::unique_ptr<ConsoleSurface>> console_surfaces;
InputRouter input;
std::set<std::string> gamepads;
std::map<std::uint64_t, PointerInput> captures;
std::optional<ConsoleId> mouse_capture;
bool console_mode = false;
bool window_focused()
{
#ifdef __EMSCRIPTEN__
	return phoenix_document_has_focus();
#else
	return IsWindowFocused();
#endif
}
std::vector<ConsoleRegion> displayed_regions;
Rml::Context *routed_context(ConsoleId route)
{
	if (!console_mode)
		return context;
	auto found = console_surfaces.find(route);
	return found == console_surfaces.end() ? nullptr : &found->second->context();
}
void cancel_captures()
{
	for (const auto &[id, point] : captures)
	{
		if (auto *context = routed_context(point.console))
			context->ProcessTouchCancel({{static_cast<Rml::TouchId>(id), {point.x, point.y}}});
		input.pointer_up(id, point.x, point.y);
	}
	captures.clear();
	if (mouse_capture)
		if (auto *context = routed_context(*mouse_capture))
			for (int button = 0; button < 3; ++button)
				context->ProcessMouseButtonUp(button, 0);
	mouse_capture.reset();
}
Screen shown = Screen::gm;
struct Navigation : Rml::EventListener
{
	void ProcessEvent(Rml::Event &event) override
	{
		if (!current_app)
			return;
		invoke_navigation(*current_app, event.GetTargetElement()->GetId());
	}
} navigation;
void update_document(Application &app)
{
	static bool labels_ready = false;
	static bool pseudo_shown = false;
	if (!labels_ready || pseudo_shown != app.pseudo_locale)
	{
		for (const auto &control : navigation_controls(app))
			if (auto *element = document->GetElementById(control.id))
				element->SetInnerRML(control.label);
		labels_ready = true;
		pseudo_shown = app.pseudo_locale;
	}
	if (shown == app.screen)
		return;
	shown = app.screen;
	document->GetElementById("heading")->SetInnerRML(screen_name(app.screen));
	document->GetElementById("host")->SetProperty("display", app.can_host ? "inline-block" : "none");
	document->GetElementById("edit")->SetProperty("display", app.can_edit ? "inline-block" : "none");
}
} // namespace
void open_window()
{
	game_screen = std::make_unique<GameScreen>();
	SetConfigFlags(FLAG_WINDOW_RESIZABLE);
	InitWindow(1280, 720, "Phoenix V3");
	SetTargetFPS(60);
	SetExitKey(KEY_NULL);
	install_native_accessibility(GetWindowHandle());
	open_devices(GetWindowHandle());
	Rml::SetSystemInterface(&system);
	Rml::SetRenderInterface(&renderer);
	if (!Rml::Initialise())
		throw std::runtime_error("RmlUi initialisation failed");
#ifdef __EMSCRIPTEN__
	const std::string font = "/assets/LatoLatin-Regular.ttf";
#else
	const std::string font = std::string(GetApplicationDirectory()) + "assets/LatoLatin-Regular.ttf";
#endif
	if (!Rml::LoadFontFace(font))
		throw std::runtime_error("Could not load player UI font: " + font);
	context = Rml::CreateContext("navigation", {GetScreenWidth(), GetScreenHeight()});
	document = context->LoadDocumentFromMemory(R"(<rml><head><style>
    body { display:block; width:100%; box-sizing:border-box; font-family:LatoLatin; font-size:20px; color:#d9e9ef; padding:20px; }
    h1, h2, p { display:block; }
    h1 { font-size:22px; color:#76c9e8; margin-bottom:6px; }
    h2 { font-size:20px; margin:4px 0; }
    #nav { display:flex; flex-wrap:wrap; }
    button { display:inline-block; width:170px; padding:8px 12px; margin:0 8px 8px 0; background:#173142; border:1px #76c9e8; }
    button:hover, button:focus { background:#315a70; }
    #nav button { width:130px; padding:4px 8px; font-size:16px; }
    #session-controls { display:block; width:100%; }
    #session-controls button { width:250px; font-size:16px; padding:4px 8px; }
    p { max-width:1100px; line-height:1.5; }
    #description { width:100%; max-height:50px; overflow:auto; font-size:16px; }
    #game { position:absolute; top:200px; left:20px; right:20px; bottom:20px; display:block; }
    #game button { width:250px; }
    .widget { position:absolute; box-sizing:border-box; border:1px #315a70; padding:10px; overflow:auto; }
    .widget h3 { display:block; font-size:20px; margin-bottom:8px; }
    .widget button { display:block; width:200px; font-size:16px; padding:4px; }
    .connection { display:block; width:100%; margin-top:12px; }
    .connection label { display:block; font-size:16px; }
    .connection input { display:block; width:700px; height:26px; font-size:18px; background:#173142; color:#d9e9ef; border:1px #76c9e8; margin:4px 0 8px; }
    </style></head><body><h1>PHOENIX / SETTLED DARK</h1>
    <div id="nav"><button id="home">Home</button><button id="host">New game</button><button id="join">Join console</button>
    <button id="view">Viewscreen</button><button id="edit">Workshop</button><button id="locale">Toggle pseudo-localisation</button></div>
    <h2 id="heading">Home</h2><p id="description"></p><div id="session-controls"></div><div id="game"></div></body></rml>)");
	if (!document)
		throw std::runtime_error("Could not load home screen");
	document->AddEventListener("click", &navigation);
	document->Show();
}
bool close_requested()
{
	return WindowShouldClose();
}
bool graphics_has_keyboard_focus()
{
#ifdef __EMSCRIPTEN__
	return phoenix_canvas_has_focus();
#else
	return IsWindowFocused();
#endif
}
void draw_screen(Application &app)
{
#ifdef __EMSCRIPTEN__
	const int width = phoenix_canvas_width(), height = phoenix_canvas_height();
	if (width != GetScreenWidth() || height != GetScreenHeight())
		SetWindowSize(width, height);
#endif
	ClearBackground({8, 15, 25, 255});
	current_app = &app;
	const bool show_consoles = app.screen == Screen::console;
	if (show_consoles != console_mode)
	{
		cancel_captures();
		console_mode = show_consoles;
	}
	app.console_surface_active = console_mode;
	app.control_placements.clear();
	std::vector<ConsoleRegion> regions =
	    console_mode && !app.console_regions.empty()
	        ? app.console_regions
	        : std::vector<ConsoleRegion>{
	              {1, 0, 0, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())}};
	for (const auto &pad : gamepads)
	{
		auto assigned = app.gamepad_assignments.find(pad);
		if (!console_mode || assigned == app.gamepad_assignments.end() ||
		    std::none_of(regions.begin(), regions.end(),
		                 [&](const auto &region)
		                 {
			                 return region.console == assigned->second;
		                 }))
			input.assign_gamepad(pad, {});
	}
	if (regions != displayed_regions)
	{
		cancel_captures();
		displayed_regions = regions;
	}
	if (!input.set_regions(regions))
	{
		cancel_captures();
		if (!input.set_regions(regions))
			throw std::runtime_error("Invalid Console display regions");
	}
	if (!input.keyboard_console())
		input.select_keyboard(regions.front().console);
	if (console_mode)
	{
		app.screen_controls.clear();
		std::erase_if(console_surfaces,
		              [&](const auto &entry)
		              {
			              return std::none_of(regions.begin(), regions.end(),
			                                  [&](const auto &region)
			                                  {
				                                  return region.console == entry.first;
			                                  });
		              });
		for (const auto &region : regions)
			if (!console_surfaces.contains(region.console))
				console_surfaces[region.console] = std::make_unique<ConsoleSurface>(region.console);
		app.screen_action = [](const std::string &id)
		{
			for (auto &[route, surface] : console_surfaces)
				if (surface->action(id))
					return true;
			return false;
		};
		app.focus_control = [](const std::string &id)
		{
			for (auto &[route, surface] : console_surfaces)
				if (id.starts_with("console/" + std::to_string(route) + "/"))
				{
					input.select_keyboard(route);
					surface->focus(id);
				}
		};
		for (const auto &region : regions)
			console_surfaces.at(region.console)->prepare(app, region, gamepads);
	}
	else
	{
		update_document(app);
		game_screen->prepare(app, *document);
		app.focus_control = [](const std::string &id)
		{
			if (auto *element = document->GetElementById(id))
				element->Focus(true);
		};
	}
	publish_semantics(app);
	drain_semantic_actions(app);
	for (const auto &event : poll_devices())
	{
		if (event.kind == DeviceEventKind::Added)
			gamepads.insert(event.device);
		else if (event.kind == DeviceEventKind::Removed)
		{
			input.disconnect_gamepad(event.device);
			app.gamepad_assignments.erase(event.device);
			gamepads.erase(event.device);
		}
		else if (event.kind == DeviceEventKind::Gamepad)
		{
			if (console_mode && window_focused())
			{
				auto found = app.gamepad_assignments.find(event.device);
				if (found != app.gamepad_assignments.end())
					input.assign_gamepad(event.device, found->second);
				input.gamepad(event.device, event.control, event.value);
			}
		}
		else
		{
			if (app.screen == Screen::workshop || app.screen == Screen::gm)
			{
				cancel_captures();
				continue;
			}
			auto point = event.kind == DeviceEventKind::Down   ? input.pointer_down(event.contact, event.x, event.y)
			             : event.kind == DeviceEventKind::Move ? input.pointer_move(event.contact, event.x, event.y)
			                                                   : input.pointer_up(event.contact, event.x, event.y);
			if (!point)
				continue;
			if (auto *target = routed_context(point->console))
			{
				Rml::TouchList touches{{static_cast<Rml::TouchId>(event.contact), {point->x, point->y}}};
				if (event.kind == DeviceEventKind::Down)
				{
					input.select_keyboard(point->console);
					target->ProcessTouchStart(touches, 0);
				}
				if (event.kind == DeviceEventKind::Move)
					target->ProcessTouchMove(touches, 0);
				if (event.kind == DeviceEventKind::Up)
					target->ProcessTouchEnd(touches, 0);
				if (event.kind == DeviceEventKind::Cancel)
					target->ProcessTouchCancel(touches);
			}
			if (point->released)
				captures.erase(event.contact);
			else
				captures[event.contact] = *point;
		}
	}
	for (const auto &event : input.drain_gamepad_inputs())
		if (console_mode && event.value > 0 && console_surfaces.contains(event.console))
			console_surfaces.at(event.console)
			    ->action("console/" + std::to_string(event.console) + "/" + event.control);
	if (app.screen == Screen::workshop || app.screen == Screen::gm)
	{
		publish_semantics(app);
		return;
	}
	if (!console_mode)
		game_screen->draw(app);
	context->SetDimensions({GetScreenWidth(), GetScreenHeight()});
	int modifiers = 0;
	if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT))
		modifiers |= Rml::Input::KM_SHIFT;
	if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))
		modifiers |= Rml::Input::KM_CTRL;
	std::optional<ConsoleId> hovered;
	for (const auto &region : regions)
		if (GetMouseX() >= region.x && GetMouseY() >= region.y && GetMouseX() < region.x + region.width &&
		    GetMouseY() < region.y + region.height)
			hovered = region.console;
	const auto mouse = mouse_capture ? mouse_capture : hovered;
	Rml::Context *mouse_context = mouse ? routed_context(*mouse) : nullptr;
	if (mouse_context)
		for (const auto &region : regions)
			if (region.console == *mouse)
				mouse_context->ProcessMouseMove(static_cast<int>(GetMouseX() - region.x),
				                                static_cast<int>(GetMouseY() - region.y), modifiers);
	for (int i = 0; i < 3; ++i)
	{
		if (mouse_context && IsMouseButtonPressed(i))
		{
			mouse_capture = mouse;
			input.select_keyboard(*mouse);
			mouse_context->ProcessMouseButtonDown(i, modifiers);
		}
		if (mouse_context && IsMouseButtonReleased(i))
			mouse_context->ProcessMouseButtonUp(i, modifiers);
	}
	if (!IsMouseButtonDown(0) && !IsMouseButtonDown(1) && !IsMouseButtonDown(2))
		mouse_capture.reset();
	if (mouse_context)
		mouse_context->ProcessMouseWheel(-GetMouseWheelMove(), modifiers);
	const std::pair<int, Rml::Input::KeyIdentifier> keys[] = {
	    {KEY_TAB, Rml::Input::KI_TAB},        {KEY_ENTER, Rml::Input::KI_RETURN},  {KEY_SPACE, Rml::Input::KI_SPACE},
	    {KEY_BACKSPACE, Rml::Input::KI_BACK}, {KEY_ESCAPE, Rml::Input::KI_ESCAPE}, {KEY_LEFT, Rml::Input::KI_LEFT},
	    {KEY_RIGHT, Rml::Input::KI_RIGHT},    {KEY_UP, Rml::Input::KI_UP},         {KEY_DOWN, Rml::Input::KI_DOWN}};
	const bool keyboard = graphics_has_keyboard_focus();
	auto *keyboard_context = input.keyboard_console() ? routed_context(*input.keyboard_console()) : context;
	if (!window_focused())
	{
		cancel_captures();
		for (const auto &pad : gamepads)
			input.assign_gamepad(pad, {});
	}
	for (auto [key, rml_key] : keys)
	{
		if (keyboard && keyboard_context && IsKeyPressed(key))
			keyboard_context->ProcessKeyDown(rml_key, modifiers);
		if (!keyboard || IsKeyReleased(key))
			for (const auto &region : regions)
				if (auto *target = routed_context(region.console))
					target->ProcessKeyUp(rml_key, modifiers);
	}
	for (int character; (character = GetCharPressed()) != 0;)
		if (keyboard && keyboard_context)
			keyboard_context->ProcessTextInput(static_cast<Rml::Character>(character));
	if (console_mode)
	{
		for (const auto &region : regions)
		{
			auto &surface = *console_surfaces.at(region.console);
			surface.context().Update();
			surface.draw(region);
			renderer.viewport({region.x, region.y, region.width, region.height});
			surface.context().Render();
			EndScissorMode();
		}
		for (auto &[name, placement] : app.control_placements)
			if (!input.keyboard_console() ||
			    !name.starts_with("console/" + std::to_string(*input.keyboard_console()) + "/"))
				placement.focused = false;
	}
	else
	{
		context->Update();
		renderer.viewport({0, 0, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())});
		context->Render();
		EndScissorMode();
		for (const auto &control : navigation_controls(app))
			if (auto *element = document->GetElementById(control.id))
			{
				auto position = element->GetAbsoluteOffset(Rml::BoxArea::Border);
				app.control_placements[control.id] = {position.x,
				                                      position.y,
				                                      element->GetOffsetWidth(),
				                                      element->GetOffsetHeight(),
				                                      context->GetFocusElement() == element,
				                                      true};
			}
	}
	publish_semantics(app);
}
void close_window()
{
	if (current_app)
		current_app->screen_action = {};
	game_screen.reset();
	cancel_captures();
	console_surfaces.clear();
	close_devices(GetWindowHandle());
	remove_native_accessibility(GetWindowHandle());
	Rml::Shutdown();
	current_app = nullptr;
	context = nullptr;
	document = nullptr;
	CloseWindow();
}
} // namespace phoenix
