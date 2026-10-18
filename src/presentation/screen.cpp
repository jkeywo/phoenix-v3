module;
#include <raylib.h>
#include <RmlUi/Core.h>
#include <FontEngineInterfaceHarfBuzz.h>
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
#include "menu.hpp"
#include "consoles.hpp"
#include "music.hpp"

namespace phoenix
{
namespace
{
UiSystem system;
UiRenderer renderer;
FontEngineInterfaceHarfBuzz shaping;
Rml::Context *context = nullptr;
Rml::ElementDocument *document = nullptr;
Application *current_app = nullptr;
std::unique_ptr<GameScreen> game_screen;
std::unique_ptr<NativeMenuScreen> native_menu;
std::unique_ptr<ServerMusic> server_music;
std::map<ConsoleId, std::unique_ptr<ConsoleSurface>> console_surfaces;
class PreviewSession final : public SessionPort
{
	Catalog catalog_;
	Observation view_;
	std::uint64_t generation_ = 1;

public:
	PreviewSession()
	{
		catalog_.entities["preview.ship"] = {{"id", "preview.ship"},
		                                     {"console", "preview.console"},
		                                     {"systems",
		                                      {{"engine", {{"max_speed_mm_s", 30000}, {"acceleration_mm_s2", 2000}}},
		                                       {"steering", {{"turn_mdeg_s", 90000}}},
		                                       {"radar", {{"range_mm", 100000}}}}}};
		view_.own = {1, "preview.ship", 0, 0, 0, 0, 0, RadarIcon::ship};
		view_.console = "main";
		view_.controls = view_.feeds = 7;
		view_.engine_enabled = view_.radar_enabled = true;
		for (unsigned i = 2; i < 16; ++i)
			view_.contacts.push_back({i, "preview.ship", static_cast<std::int64_t>(i % 4) * 17000 - 25000,
			                          static_cast<std::int64_t>(i / 4) * 19000 - 25000, 0, 0,
			                          static_cast<std::int64_t>(i) * 23000, static_cast<RadarIcon>(1 + i % 3)});
	}
	void layout(Json value)
	{
		value["id"] = "preview.console";
		if (catalog_.consoles["preview.console"] != value)
		{
			catalog_.consoles["preview.console"] = std::move(value);
			++generation_;
		}
	}
	void add_pack(const Bytes &) override
	{
	}
	void clear_packs() override
	{
	}
	const Catalog &catalog() const override
	{
		return catalog_;
	}
	void start(const GameSetup &) override
	{
	}
	bool running() const override
	{
		return true;
	}
	bool recovery_pending() const override
	{
		return false;
	}
	Bytes save() const override
	{
		return {};
	}
	void restore(const Bytes &) override
	{
	}
	void resume_local() override
	{
	}
	void tick() override
	{
	}
	std::uint64_t ticks() const override
	{
		return 1;
	}
	std::uint64_t generation() const override
	{
		return generation_;
	}
	bool command(const OperatingCommand &value) override
	{
		if (value.target != 1)
			return false;
		if (value.action == "speed_mm_s")
			view_.requested_speed = std::clamp<std::int64_t>(value.value, 0, 30000);
		else if (value.action == "heading_mdeg")
			view_.requested_heading = value.value;
		else
			return false;
		return true;
	}
	std::optional<Observation> observation() const override
	{
		return view_;
	}
	std::vector<BodyView> viewscreen() const override
	{
		return {};
	}
};
std::shared_ptr<PreviewSession> preview_session;
Application preview_app;
std::unique_ptr<ConsoleSurface> preview_surface;
RenderTexture2D preview_texture{};
InputRouter input;
std::set<std::string> gamepads;
std::map<std::uint64_t, PointerInput> captures;
std::set<std::uint64_t> viewscreen_contacts;
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
	for (auto id : viewscreen_contacts)
		context->ProcessTouchCancel({{static_cast<Rml::TouchId>(id), {0, 0}}});
	viewscreen_contacts.clear();
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
		if (event.GetType() == "change")
		{
			const auto id = event.GetTargetElement()->GetId();
			if (id == "menu-volume")
			{
				set_music_volume(*current_app, event.GetParameter<Rml::String>("value", ""));
				return;
			}
			if (id == "join-code" || id == "join-service" || id == "join-profile" || id == "player-name" ||
			    id.starts_with("local-name/"))
				if (auto *field = dynamic_cast<Rml::ElementFormControl *>(event.GetTargetElement()))
				{
					if (id == "join-code")
						current_app->network_code = field->GetValue();
					if (id == "join-service")
						current_app->network_service = field->GetValue();
					if (id == "join-profile")
						current_app->network_profile = field->GetValue();
					if (id == "player-name")
						current_app->player_name = field->GetValue();
					if (id.starts_with("local-name/"))
						current_app->local_player_names[std::stoull(id.substr(11))] = field->GetValue();
				}
			if (id == "music-volume")
				set_music_volume(*current_app, event.GetParameter<Rml::String>("value", ""));
			return;
		}
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
			if (!control.editable)
				if (auto *element = document->GetElementById(control.id))
					element->SetInnerRML(control.label);
		labels_ready = true;
		pseudo_shown = app.pseudo_locale;
	}
	if (shown == app.screen)
		return;
	shown = app.screen;
	document->GetElementById("heading")->SetInnerRML(localise(screen_name(app.screen)));
	document->GetElementById("host")->SetProperty("display", app.can_host ? "inline-block" : "none");
	document->GetElementById("edit")->SetProperty("display", app.can_edit ? "inline-block" : "none");
	document->GetElementById("settings")->SetProperty("display", app.can_host ? "inline-block" : "none");
}
void prepare_settings(Application &app)
{
	const bool visible = app.screen == Screen::settings && (app.can_host || game_active(app));
	document->GetElementById("settings-panel")->SetProperty("display", visible ? "block" : "none");
	for (const char *id : {"game", "session-controls", "description"})
		document->GetElementById(id)->SetProperty("display", visible ? "none" : "block");
	if (!visible)
		return;
	app.tools_keyboard_focus = false;
	app.screen_controls = {{"settings.close", "Back"}};
	const bool can_quit = game_active(app) && bool(app.return_home);
	document->GetElementById("settings.quit")->SetProperty("display", can_quit ? "block" : "none");
	if (can_quit)
		app.screen_controls.emplace_back("settings.quit", "Quit to Main Menu");
	for (const auto &[id, text] : std::vector<std::pair<std::string, std::string>>{
	         {"settings.dust", app.dust_motes ? "Dust motes: On" : "Dust motes: Off"},
	         {"settings.grid", app.reference_grid ? "Reference grid: On" : "Reference grid: Off"},
	         {"settings.motion", app.reduced_motion ? "Reduced motion: On" : "Reduced motion: Off"}})
	{
		app.screen_controls.emplace_back(id, text);
		if (auto *element = document->GetElementById(id))
			element->SetInnerRML(localise(text));
	}
	auto *approvals = document->GetElementById("settings-approvals");
	approvals->SetInnerRML("");
	if (game_active(app))
		for (const auto &[id, text] : app.session->lobby_controls())
			if (id.starts_with("lobby.approve/") || id.starts_with("network.approve/") || id.starts_with("network.gm/"))
			{
				app.screen_controls.emplace_back(id, text);
				auto button = document->CreateElement("button");
				button->SetId(id);
				button->AppendChild(document->CreateTextNode(localise(text)));
				approvals->AppendChild(std::move(button));
			}
	app.screen_action = [&app](const std::string &id)
	{
		if (app.screen != Screen::settings || !app.session)
			return false;
		for (const auto &[control, text] : app.screen_controls)
			if (control == id && (id.starts_with("lobby.approve/") || id.starts_with("network.approve/") ||
			                      id.starts_with("network.gm/")))
				return app.session->session_action(id);
		return false;
	};
	const auto value = std::to_string(app.music_volume);
	auto *slider = dynamic_cast<Rml::ElementFormControl *>(document->GetElementById("music-volume"));
	if (slider->GetAttribute<Rml::String>("value", "") != value)
		slider->SetValue(value);
	app.screen_inputs.push_back({"music-volume", "Music volume", value, true});
	for (const auto &[id, text] :
	     std::vector<std::pair<std::string, std::string>>{{"music-label", localise("Music volume")},
	                                                      {"music-percent", value + "%"},
	                                                      {"settings.close", localise("Back")},
	                                                      {"settings.quit", localise("Quit to Main Menu")}})
		if (auto *element = document->GetElementById(id); element->GetInnerRML() != text)
			element->SetInnerRML(text);
	app.accessible_status = localise("Music volume") + ": " + value + "%";
}
void prepare_runtime_chrome(Application &app)
{
	const bool view = app.screen == Screen::viewscreen;
	if (!view && !game_active(app))
	{
		auto *nav = document->GetElementById("nav");
		nav->RemoveProperty("position");
		nav->RemoveProperty("right");
		nav->RemoveProperty("top");
		return;
	}
	const bool settings = app.screen == Screen::settings;
	for (const char *id : {"app-title", "heading", "description", "session-controls", "game"})
		if (view || settings)
			document->GetElementById(id)->SetProperty("display", "none");
	for (const char *id : {"home", "lobby", "host", "join", "view", "edit", "locale", "settings"})
		document->GetElementById(id)->SetProperty("display", view && std::string(id) == "settings" ? "block" : "none");
	auto *nav = document->GetElementById("nav");
	nav->SetProperty("display", view ? "block" : "none");
	nav->SetProperty("position", "absolute");
	nav->SetProperty("right", "16px");
	nav->SetProperty("top", "16px");
	if (app.viewscreen_region && view)
	{
		const auto &r = *app.viewscreen_region;
		nav->SetProperty("right", "auto");
		nav->SetProperty("width", "200px");
		nav->SetProperty("left", std::to_string(r.x + r.width - 216) + "px");
		nav->SetProperty("top", std::to_string(r.y + 16) + "px");
	}
	else
	{
		nav->SetProperty("left", "auto");
		nav->SetProperty("width", "auto");
	}
}
} // namespace
bool probe_menu_keyboard(Application &app)
{
	if (!app.native_menu)
		return false;
	const auto controls = navigation_controls(app);
	if (controls.empty())
		return false;
	if (auto *element = document->GetElementById(controls.front().id))
		element->Focus(true);
	else
		return false;
	for (std::size_t i = 0; i < controls.size() + 2; ++i)
	{
		context->ProcessKeyDown(Rml::Input::KI_TAB, 0);
		context->ProcessKeyUp(Rml::Input::KI_TAB, 0);
		const auto *focused = context->GetFocusElement();
		if (!focused || std::none_of(controls.begin(), controls.end(),
		                             [&](const auto &c)
		                             {
			                             return c.id == focused->GetId();
		                             }))
			return false;
	}
	return true;
}
void open_window()
{
	game_screen = std::make_unique<GameScreen>();
	native_menu = std::make_unique<NativeMenuScreen>();
	server_music = std::make_unique<ServerMusic>();
	SetConfigFlags(FLAG_WINDOW_RESIZABLE);
	InitWindow(1280, 720, "Phoenix V3");
	SetTargetFPS(60);
	SetExitKey(KEY_NULL);
	install_native_accessibility(GetWindowHandle());
	open_devices(GetWindowHandle());
	Rml::SetSystemInterface(&system);
	Rml::SetRenderInterface(&renderer);
	shaping.RegisterLanguage("en", "Latn", TextFlowDirection::LeftToRight);
	shaping.RegisterLanguage("ar", "Arab", TextFlowDirection::RightToLeft);
	shaping.RegisterLanguage("hi", "Deva", TextFlowDirection::LeftToRight);
	Rml::SetFontEngineInterface(&shaping);
	if (!Rml::Initialise())
		throw std::runtime_error("RmlUi initialisation failed");
#ifdef __EMSCRIPTEN__
	const std::string font = "/assets/LatoLatin-Regular.ttf";
#else
	const std::string font = std::string(GetApplicationDirectory()) + "assets/LatoLatin-Regular.ttf";
#endif
	if (!Rml::LoadFontFace(font))
		throw std::runtime_error("Could not load player UI font: " + font);
	const auto font_directory = std::filesystem::path(font).parent_path();
	if (!Rml::LoadFontFace((font_directory / "UnifrakturCook-Bold.ttf").generic_string()))
		throw std::runtime_error("Could not load menu title font");
	configure_translations(
	    parse_json(read_file((font_directory / "locales.json").generic_string(), 1024 * 1024), "UI translations"));
	for (const char *fallback : {"shaping-Cairo-Regular.ttf", "shaping-Poppins-Regular.ttf"})
		if (!Rml::LoadFontFace((font_directory / fallback).generic_string(), true))
			throw std::runtime_error("Could not load Unicode fallback font");
	context = Rml::CreateContext("navigation", {GetScreenWidth(), GetScreenHeight()});
	document = context->LoadDocumentFromMemory(std::string(R"(<rml><head><style>
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
    #settings-panel { display:none; margin-top:20px; }
    #settings-panel label, #music-percent { display:block; margin-bottom:12px; }
    #music-volume { display:block; width:80%; max-width:600px; height:40px; margin-bottom:16px; }
    #music-volume slidertrack { height:8px; margin:16px 0; background:#315a70; }
    #music-volume sliderbar { width:24px; height:40px; background:#76c9e8; border:2px #d9e9ef; }
    #music-volume:focus sliderbar { background:#d9e9ef; border-color:#76c9e8; }
    #music-volume sliderarrowdec, #music-volume sliderarrowinc { display:none; }
    #native-menu { position:absolute; overflow:hidden; background:#080f19; padding:0; box-sizing:border-box; }
    #native-menu h1 { position:absolute; top:30px; left:0; width:100%; text-align:center; font-size:34px; letter-spacing:3px; }
    #native-menu h2 { font-size:22px; margin:16px 0 12px; }
    #native-menu button { display:block; box-sizing:border-box; width:100%; min-height:48px; margin:0 0 12px; padding:12px; font-size:18px; }
    #native-menu .menu-selected { background:#315a70; border:2px #d9e9ef; }
    #native-menu p { word-break:break-word; }
    #native-menu scrollbarhorizontal { height:14px; background:#102535; }
    #native-menu scrollbarhorizontal sliderbar { min-width:24px; background:#76c9e8; }
    #native-menu scrollbarvertical { width:14px; background:#102535; }
    #native-menu scrollbarvertical sliderbar { min-height:24px; background:#76c9e8; }
    #native-menu button:disabled { color:#617785; background:#10202b; border-color:#243b49; }
    .menu-top { position:absolute; top:18px; left:20px; width:140px; }
    .menu-bottom { position:absolute; bottom:20px; left:20px; width:160px; }
    .menu-back { position:absolute; bottom:20px; right:20px; width:140px; }
    #menu-viewport { position:absolute; left:0; top:120px; right:0; bottom:100px; overflow:auto; }
    #menu-columns { position:relative; display:flex; flex-direction:row; align-items:flex-start; height:100%; }
    .menu-column { display:block; flex-shrink:0; padding-right:20px; box-sizing:border-box; max-height:100%; overflow:auto; }
    .menu-column input { box-sizing:border-box; width:100%; height:44px; margin:8px 0 16px; }
    #native-menu .connection { margin-top:0; }
    .lobby-panel { position:absolute; top:110px; left:15%; width:70%; bottom:110px; overflow:auto; }
    .lobby-panel button { max-width:500px; }
    .modal-shade { position:absolute; top:0; left:0; width:100%; height:100%; background:#080f19dd; }
    .menu-dialog { position:absolute; top:12%; left:20%; width:60%; max-height:76%; padding:24px; box-sizing:border-box; background:#102535; border:2px #76c9e8; overflow:auto; }
    #menu-status { position:absolute; left:200px; right:180px; bottom:14px; max-height:72px; overflow:auto; font-size:16px; }
    #menu-volume { display:block; width:100%; height:44px; margin-top:14px; }
    #menu-volume slidertrack { height:8px; margin:18px 0; background:#315a70; }
    #menu-volume sliderbar { width:24px; height:44px; background:#76c9e8; border:2px #d9e9ef; }
    #menu-volume sliderarrowdec, #menu-volume sliderarrowinc { display:none; }
    )") + native_menu->style_sheet(font_directory.generic_string()) +
	                                               R"(</style></head><body><h1 id="app-title">Against the Stars</h1>
    <div id="nav"><button id="home">Home</button><button id="lobby" style="display:none;">Lobby</button><button id="host">New game</button><button id="join">Join console</button>
    <button id="view">Viewscreen</button><button id="edit">Workshop</button><button id="locale">Toggle pseudo-localisation</button><button id="settings">Settings</button></div>
    <h2 id="heading">Home</h2><p id="description"></p><div id="session-controls"></div><div id="game"></div>
    <div id="settings-panel"><label id="music-label" for="music-volume">Music volume</label>
    <input id="music-volume" type="range" min="0" max="100" step="1" value="50"/>
    <span id="music-percent">50%</span><button id="settings.dust">Dust motes: On</button><button id="settings.grid">Reference grid: On</button><button id="settings.motion">Reduced motion: Off</button><div id="settings-approvals"></div><button id="settings.quit">Quit to Main Menu</button><button id="settings.close">Back</button></div></body></rml>)",
	                                           (font_directory / "navigation.rml").generic_string());
	if (!document)
		throw std::runtime_error("Could not load home screen");
	document->AddEventListener("click", &navigation);
	document->AddEventListener("change", &navigation);
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
	const auto frame_started = GetTime();
#ifdef __EMSCRIPTEN__
	const int width = phoenix_canvas_width(), height = phoenix_canvas_height();
	if (width != GetScreenWidth() || height != GetScreenHeight())
		SetWindowSize(width, height);
#endif
	ClearBackground({8, 15, 25, 255});
	app.frame_metrics["clear"] = GetTime() - frame_started;
	const auto preparation_started = GetTime();
	current_app = &app;
	native_menu->poll(app);
	select_language(app.language, app.pseudo_locale);
	if ((app.can_host || app.native_menu) && app.screen != Screen::workshop && app.screen != Screen::gm &&
	    IsKeyPressed(KEY_ESCAPE) && graphics_has_keyboard_focus())
		invoke_navigation(app, app.native_menu && (app.screen == Screen::home || app.screen == Screen::server_lobby ||
		                                           app.screen == Screen::client_lobby ||
		                                           app.screen == Screen::settings || app.packs_popup)
		                           ? "menu.back"
		                           : "settings");
	const bool show_consoles = app.screen == Screen::console ||
	                           (app.can_host && app.screen == Screen::viewscreen && !app.console_regions.empty());
	if (show_consoles != console_mode)
	{
		cancel_captures();
		console_mode = show_consoles;
	}
	app.console_surface_active = console_mode;
	app.control_placements.clear();
	app.screen_inputs.clear();
	app.screen_readouts.clear();
	app.set_control_value = {};
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
		if (app.screen == Screen::viewscreen)
		{
			document->GetElementById("native-menu")->SetProperty("display", "none");
			prepare_runtime_chrome(app);
		}
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
		app.focus_control = [&app](const std::string &id)
		{
			for (auto &[route, surface] : console_surfaces)
				if (id.starts_with("console/" + std::to_string(route) + "/"))
				{
					input.select_keyboard(route);
					app.tools_keyboard_focus = false;
					surface->focus(id);
				}
		};
		for (const auto &region : regions)
			console_surfaces.at(region.console)->prepare(app, region, gamepads);
	}
	else
	{
		update_document(app);
		if (!native_menu->prepare(app, *document))
		{
			prepare_settings(app);
			if (app.screen != Screen::settings)
				game_screen->prepare(app, *document);
		}
		prepare_runtime_chrome(app);
		if (app.text_probe && !document->GetElementById("probe-text"))
			document->GetElementById("game")->SetInnerRML(R"(<h2>Unicode shaping and text input</h2>
<p lang="ar" dir="rtl" style="font-family:Cairo;font-size:32px;text-align:right;">&#1605;&#1585;&#1581;&#1576;&#1575; &#1576;&#1603;&#1605; &#1601;&#1610; &#1601;&#1610;&#1606;&#1610;&#1603;&#1587;</p>
<p lang="hi" style="font-family:Poppins;font-size:32px;">&#2344;&#2350;&#2360;&#2381;&#2340;&#2375; &#2342;&#2369;&#2344;&#2367;&#2351;&#2366;</p>
<p>Latin fallback: caf&#233; / fa&#231;ade / &#233;&#769;</p>
<div class="connection"><label>Unicode text input</label><input id="probe-text" type="text" maxlength="128"/></div>)");
		for (const auto &[id, label] :
		     std::vector<std::pair<std::string, std::string>>{{"join-code", "Network code (room/server)"},
		                                                      {"join-service", "Network service"},
		                                                      {"join-profile", "Local profile"},
		                                                      {"join-page", "Launcher page URL for joining devices"},
		                                                      {"probe-text", "Unicode text input"}})
			if (auto *field = dynamic_cast<Rml::ElementFormControl *>(document->GetElementById(id)))
				if (app.screen != Screen::settings && !app.native_menu)
					app.screen_inputs.push_back({id, label, field->GetValue()});
		app.set_control_value = [&app](const std::string &id, const std::string &value)
		{
			if (id == "music-volume" || id == "menu-volume")
				return set_music_volume(app, value);
			if (value.size() > 4096)
				return false;
			if (auto *field = dynamic_cast<Rml::ElementFormControl *>(document->GetElementById(id)))
			{
				field->SetValue(value);
				if (id == "join-code")
					app.network_code = value;
				if (id == "join-service")
					app.network_service = value;
				if (id == "join-profile")
					app.network_profile = value;
				if (id == "player-name")
					app.player_name = value;
				if (id.starts_with("local-name/"))
					app.local_player_names[std::stoull(id.substr(11))] = value;
				return true;
			}
			return false;
		};
		app.focus_control = [](const std::string &id)
		{
			if (auto *element = document->GetElementById(id))
				element->Focus(true);
		};
	}
	app.frame_metrics["preparation"] = GetTime() - preparation_started;
	server_music->update(app);
	const auto semantics_started = GetTime();
	publish_semantics(app);
	drain_semantic_actions(app);
	app.frame_metrics["semantics"] = GetTime() - semantics_started;
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
			if (!console_mode && app.native_menu && window_focused() && event.value > 0)
			{
				if (event.control == "game.contacts")
					context->ProcessKeyDown(Rml::Input::KI_RETURN, 0);
				else
					context->ProcessKeyDown(
					    Rml::Input::KI_TAB,
					    event.control == "game.faster" || event.control == "game.left" ? Rml::Input::KM_SHIFT : 0);
			}
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
			if (console_mode && app.screen == Screen::viewscreen && app.viewscreen_region)
			{
				const auto &r = *app.viewscreen_region;
				if (viewscreen_contacts.contains(event.contact) ||
				    (event.kind == DeviceEventKind::Down &&
				     CheckCollisionPointRec({event.x, event.y}, {r.x, r.y, r.width, r.height})))
				{
					Rml::TouchList touch{{static_cast<Rml::TouchId>(event.contact), {event.x, event.y}}};
					if (event.kind == DeviceEventKind::Down)
					{
						viewscreen_contacts.insert(event.contact);
						context->ProcessTouchStart(touch, 0);
					}
					if (event.kind == DeviceEventKind::Move)
						context->ProcessTouchMove(touch, 0);
					if (event.kind == DeviceEventKind::Up)
					{
						context->ProcessTouchEnd(touch, 0);
						viewscreen_contacts.erase(event.contact);
					}
					if (event.kind == DeviceEventKind::Cancel)
					{
						context->ProcessTouchCancel(touch);
						viewscreen_contacts.erase(event.contact);
					}
					continue;
				}
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
	if (console_mode && app.screen == Screen::viewscreen && app.viewscreen_region)
	{
		const auto &r = *app.viewscreen_region;
		const bool over_view =
		    !mouse_capture && CheckCollisionPointRec(GetMousePosition(), {r.x, r.y, r.width, r.height});
		context->ProcessMouseMove(over_view ? GetMouseX() : -1, over_view ? GetMouseY() : -1, modifiers);
		if (over_view && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
			context->ProcessMouseButtonDown(0, modifiers);
		if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
			context->ProcessMouseButtonUp(0, modifiers);
	}
	const std::pair<int, Rml::Input::KeyIdentifier> keys[] = {
	    {KEY_TAB, Rml::Input::KI_TAB},        {KEY_ENTER, Rml::Input::KI_RETURN},  {KEY_SPACE, Rml::Input::KI_SPACE},
	    {KEY_BACKSPACE, Rml::Input::KI_BACK}, {KEY_ESCAPE, Rml::Input::KI_ESCAPE}, {KEY_LEFT, Rml::Input::KI_LEFT},
	    {KEY_RIGHT, Rml::Input::KI_RIGHT},    {KEY_UP, Rml::Input::KI_UP},         {KEY_DOWN, Rml::Input::KI_DOWN}};
	const bool tool_keyboard = app.screen == Screen::workshop || app.screen == Screen::gm || app.tools_keyboard_focus;
	const bool keyboard = graphics_has_keyboard_focus() && !tool_keyboard;
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
	if (!tool_keyboard)
		for (int character; (character = GetCharPressed()) != 0;)
			if (keyboard && keyboard_context)
				keyboard_context->ProcessTextInput(static_cast<Rml::Character>(character));
	if (console_mode)
	{
		const auto scene_started = GetTime();
		if (app.viewscreen_region)
		{
			const auto &r = *app.viewscreen_region;
			game_screen->draw_viewscreen(app, {r.x, r.y, r.width, r.height});
		}
		app.frame_metrics["scene"] = GetTime() - scene_started;
		if (app.screen == Screen::viewscreen)
		{
			context->Update();
			renderer.viewport({0, 0, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())});
			context->Render();
			renderer.finish();
			if (auto *settings = document->GetElementById("settings"))
			{
				const auto at = settings->GetAbsoluteOffset(Rml::BoxArea::Border);
				app.control_placements["settings"] = {at.x,
				                                      at.y,
				                                      settings->GetOffsetWidth(),
				                                      settings->GetOffsetHeight(),
				                                      context->GetFocusElement() == settings,
				                                      true};
			}
		}
		const auto consoles_started = GetTime();
		for (const auto &region : regions)
		{
			auto &surface = *console_surfaces.at(region.console);
			surface.context().Update();
			surface.draw(region);
			renderer.viewport({region.x, region.y, region.width, region.height});
			surface.context().Render();
			renderer.finish();
		}
		app.frame_metrics["consoles"] = GetTime() - consoles_started;
		for (auto &[name, placement] : app.control_placements)
			if (!input.keyboard_console() ||
			    !name.starts_with("console/" + std::to_string(*input.keyboard_console()) + "/"))
				placement.focused = false;
	}
	else
	{
		context->Update();
		native_menu->position_labels(*document);
		native_menu->draw_backdrop(app, *document);
		renderer.viewport({0, 0, static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())});
		context->Render();
		renderer.finish();
		native_menu->draw(app, *document);
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
	preview_surface.reset();
	preview_session.reset();
	preview_app.session.reset();
	if (preview_texture.id)
		UnloadRenderTexture(preview_texture);
	preview_texture = {};
	if (current_app)
		current_app->screen_action = {};
	game_screen.reset();
	native_menu.reset();
	server_music.reset();
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
Texture2D draw_console_preview(const Json &layout, int width, int height, Vector2 pointer, bool pressed, bool released,
                               float wheel, bool pseudo)
{
	width = std::clamp(width, 100, 1920);
	height = std::clamp(height, 100, 1920);
	if (!preview_session)
	{
		preview_session = std::make_shared<PreviewSession>();
		preview_app.session = preview_session;
		preview_app.screen = Screen::console;
		preview_surface = std::make_unique<ConsoleSurface>(1, "preview-", false);
	}
	preview_session->layout(layout);
	preview_app.pseudo_locale = pseudo;
	preview_app.screen_controls.clear();
	preview_app.control_placements.clear();
	ConsoleRegion region{1, 0, 0, static_cast<float>(width), static_cast<float>(height)};
	preview_surface->prepare(preview_app, region, {});
	auto &context = preview_surface->context();
	context.ProcessMouseMove(static_cast<int>(pointer.x), static_cast<int>(pointer.y), 0);
	if (pressed)
		context.ProcessMouseButtonDown(0, 0);
	if (released)
		context.ProcessMouseButtonUp(0, 0);
	if (wheel)
		context.ProcessMouseWheel(-wheel, 0);
	context.Update();
	if (!preview_texture.id || preview_texture.texture.width != width || preview_texture.texture.height != height)
	{
		if (preview_texture.id)
			UnloadRenderTexture(preview_texture);
		preview_texture = LoadRenderTexture(width, height);
	}
	BeginTextureMode(preview_texture);
	ClearBackground({8, 15, 25, 255});
	preview_surface->draw(region);
	renderer.viewport({0, 0, static_cast<float>(width), static_cast<float>(height)});
	context.Render();
	renderer.finish();
	EndTextureMode();
	return preview_texture.texture;
}
} // namespace phoenix
