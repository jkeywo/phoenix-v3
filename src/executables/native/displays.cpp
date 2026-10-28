module;
#include <raylib.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <map>
#include <cmath>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

module phoenix.native.displays;
import phoenix.application.displays;
import phoenix.application.preference_store;
import phoenix.platform.display_identity;

namespace phoenix
{
NativeDisplayAdapter::NativeDisplayAdapter(Application &application, unsigned frame_limit, bool vsync, bool uncapped)
    : app(application), frame_limit(frame_limit), vsync(vsync), uncapped(uncapped)
{
	app.host_display = GetCurrentMonitor();
	update_native_displays(app, enumerate_displays());
	app.apply_displays = [this]
	{
		apply();
	};
	app.reset_displays = [this]
	{
		reset();
	};
}
NativeDisplayAdapter::~NativeDisplayAdapter()
{
	app.apply_displays = {};
	app.reset_displays = {};
}
std::vector<Application::NativeDisplay> NativeDisplayAdapter::enumerate_displays()
{
	std::vector<phoenix::Application::NativeDisplay> displays;
	for (int monitor = 0; monitor < GetMonitorCount() && displays.size() < 6; ++monitor)
	{
		const auto position = GetMonitorPosition(monitor);
		if (std::any_of(displays.begin(), displays.end(),
		                [&](const auto &display)
		                {
			                const auto other = GetMonitorPosition(display.id);
			                return other.x == position.x && other.y == position.y;
		                }))
			continue;
		displays.push_back({monitor, "Screen " + std::to_string(monitor + 1) + " (" + GetMonitorName(monitor) + ")",
		                    phoenix::native_monitor_identity(
		                        static_cast<int>(position.x), static_cast<int>(position.y),
		                        std::string(GetMonitorName(monitor)) + "/" + std::to_string(GetMonitorWidth(monitor)) +
		                            "x" + std::to_string(GetMonitorHeight(monitor)) + "/" + std::to_string(monitor))});
	}
	return displays;
}
void NativeDisplayAdapter::pace()
{
	SetTargetFPS(frame_limit                                                            ? static_cast<int>(frame_limit)
	             : (vsync || uncapped || !app.console_regions.empty() || app.gm_region) ? 0
	                                                                                    : 60);
}
void NativeDisplayAdapter::apply()
{
	if (app.display_consoles.empty() && app.inspector_display < 0)
	{
		if (lobby_span && app.reset_displays)
			app.reset_displays();
		return;
	}
	std::vector<int> monitors{app.host_display};
	for (const auto &[monitor, console] : app.display_consoles)
		monitors.push_back(monitor);
	if (app.inspector_display >= 0)
		monitors.push_back(app.inspector_display);
	float left = GetMonitorPosition(app.host_display).x, top = GetMonitorPosition(app.host_display).y;
	float right = left, bottom = top;
	for (const auto monitor : monitors)
	{
		const auto position = GetMonitorPosition(monitor);
		left = std::min(left, position.x);
		top = std::min(top, position.y);
		right = std::max(right, position.x + GetMonitorWidth(monitor));
		bottom = std::max(bottom, position.y + GetMonitorHeight(monitor));
	}
	std::vector<phoenix::ConsoleRegion> all;
	app.console_regions.clear();
	app.viewscreen_region.reset();
	app.gm_region.reset();
	for (const auto monitor : monitors)
	{
		const auto position = GetMonitorPosition(monitor);
		phoenix::ConsoleRegion region{static_cast<std::uint64_t>(monitor + 2), position.x - left, position.y - top,
		                              static_cast<float>(GetMonitorWidth(monitor)),
		                              static_cast<float>(GetMonitorHeight(monitor))};
		all.push_back(region);
		if (monitor == app.host_display)
			app.viewscreen_region = region;
		else if (monitor == app.inspector_display)
			app.gm_region = region;
		else
			app.console_regions.push_back(region);
	}
	phoenix::InputRouter validator;
	if (all.size() > 6 || !validator.set_regions(all))
		throw std::runtime_error("Assigned screens overlap or exceed the six-screen limit");
	SetWindowState(FLAG_WINDOW_UNDECORATED);
	ClearWindowState(FLAG_WINDOW_RESIZABLE);
	SetWindowPosition(static_cast<int>(left), static_cast<int>(top));
	SetWindowSize(static_cast<int>(right - left), static_cast<int>(bottom - top));
	lobby_span = true;
	pace();
}
void NativeDisplayAdapter::reset()
{
	if (lobby_span)
	{
		ClearWindowState(FLAG_WINDOW_UNDECORATED);
		SetWindowState(FLAG_WINDOW_RESIZABLE);
		SetWindowSize(1280, 720);
		const auto position = GetMonitorPosition(app.host_display);
		SetWindowPosition(static_cast<int>(position.x + 40), static_cast<int>(position.y + 40));
		app.console_regions.clear();
		app.viewscreen_region.reset();
		app.gm_region.reset();
	}
	app.display_consoles.clear();
	app.local_player_names.clear();
	app.inspector_display = -1;
	lobby_span = false;
	pace();
}
void NativeDisplayAdapter::configure(bool span_displays, const std::string &display_layout)
{
	if (!display_layout.empty())
		layout_identity =
		    std::filesystem::absolute(std::filesystem::u8path(display_layout)).lexically_normal().generic_string();
	if (span_displays && display_layout.empty())
	{
		const int count = GetMonitorCount();
		float left = 0, top = 0, right = 0, bottom = 0;
		for (int monitor = 0; monitor < count; ++monitor)
		{
			const auto position = GetMonitorPosition(monitor);
			const float width = static_cast<float>(GetMonitorWidth(monitor)),
			            height = static_cast<float>(GetMonitorHeight(monitor));
			if (app.console_regions.size() == 6 || std::any_of(app.console_regions.begin(), app.console_regions.end(),
			                                                   [&](const auto &region)
			                                                   {
				                                                   return position.x < region.x + region.width &&
				                                                          position.x + width > region.x &&
				                                                          position.y < region.y + region.height &&
				                                                          position.y + height > region.y;
			                                                   }))
				continue;
			if (!monitor)
			{
				left = position.x;
				top = position.y;
			}
			left = std::min(left, position.x);
			top = std::min(top, position.y);
			right = std::max(right, position.x + width);
			bottom = std::max(bottom, position.y + height);
			app.console_regions.push_back(
			    {static_cast<std::uint64_t>(app.console_regions.size() + 1), position.x, position.y, width, height});
			std::cout << "Display " << monitor + 1 << ": " << GetMonitorName(monitor) << " / " << position.x << ','
			          << position.y << " / " << width << 'x' << height << '\n';
		}
		for (auto &region : app.console_regions)
		{
			region.x -= left;
			region.y -= top;
		}
		if (app.console_regions.size() > 1)
		{
			app.viewscreen_region = app.console_regions.front();
			app.console_regions.erase(app.console_regions.begin());
		}
		SetWindowState(FLAG_WINDOW_UNDECORATED);
		ClearWindowState(FLAG_WINDOW_RESIZABLE);
		SetWindowSize(static_cast<int>(right - left), static_cast<int>(bottom - top));
		SetWindowPosition(static_cast<int>(left), static_cast<int>(top));
	}

	if (!display_layout.empty())
	{

		const auto bytes = phoenix::read_file(display_layout);
		const auto layout = nlohmann::json::parse(bytes);
		const int width = layout.at("width"), height = layout.at("height");
		if (width < 320 || height < 240 || width > 16384 || height > 16384)
			throw std::runtime_error("Invalid display window dimensions");
		std::vector<phoenix::ConsoleRegion> all_regions;
		for (const auto &region : layout.at("regions"))
		{
			phoenix::ConsoleRegion value{region.at("route"), region.at("x"), region.at("y"), region.at("width"),
			                             region.at("height")};
			if (value.x < 0 || value.y < 0 || value.x + value.width > width || value.y + value.height > height)
				throw std::runtime_error("Console region is outside the display window");
			all_regions.push_back(value);
			if (region.value("role", std::string("console")) == "viewscreen")
			{
				if (app.viewscreen_region)
					throw std::runtime_error("Only one Viewscreen region is supported");
				app.viewscreen_region = value;
			}
			else if (region.value("role", std::string("console")) == "gm")
			{
				if (app.gm_region || !app.can_host)
					throw std::runtime_error("A local GM region requires one native host");
				app.gm_region = value;
			}
			else if (region.value("role", std::string("console")) == "console")
				app.console_regions.push_back(value);
			else
				throw std::runtime_error("Unknown display region role");
		}
		phoenix::InputRouter validator;
		if (app.console_regions.empty() || all_regions.size() > 6 || !validator.set_regions(all_regions))
			throw std::runtime_error("Display layout requires one to six non-overlapping Console regions");
		SetWindowState(FLAG_WINDOW_UNDECORATED);
		ClearWindowState(FLAG_WINDOW_RESIZABLE);
		SetWindowSize(width, height);
		SetWindowPosition(layout.value("x", 0), layout.value("y", 0));
	}
	if (vsync)
		SetWindowState(FLAG_VSYNC_HINT);
	pace();
}
void NativeDisplayAdapter::refresh_device_routes()
{
	// Device identities follow the live OS inventory even while gameplay keeps
	// its existing Console/layout assignments, or an explicit layout is in use.
	if (GetTime() >= next_device_refresh)
	{
		next_device_refresh = GetTime() + .5;
		auto detected = enumerate_displays();
		if (!detected.empty())
			device_displays = std::move(detected);
	}
	std::map<ConsoleId, std::string> routes;
	const auto origin = GetWindowPosition();
	auto resolve = [&](const ConsoleRegion &region)
	{
		for (const auto &display : device_displays)
		{
			const auto pos = GetMonitorPosition(display.id);
			if (std::abs(origin.x + region.x - pos.x) < 2 && std::abs(origin.y + region.y - pos.y) < 2 &&
			    std::abs(region.width - GetMonitorWidth(display.id)) < 2 &&
			    std::abs(region.height - GetMonitorHeight(display.id)) < 2)
				return display.identity;
		}
		// Keep a disconnected physical screen's preference key. Replacing it with
		// a new virtual key would silently select System default after HDMI loss.
		if (const auto previous = app.screen_identities.find(region.console); previous != app.screen_identities.end())
			return previous->second;
		return "layout:" + layout_identity + "/" + std::to_string(region.console);
	};
	for (const auto &display : device_displays)
		if (display.id == (app.console_regions.empty() ? GetCurrentMonitor() : app.host_display))
			routes[0] = display.identity;
	for (const auto &region : app.console_regions)
		routes[region.console] = resolve(region);
	if (app.viewscreen_region)
	{
		routes[0] = resolve(*app.viewscreen_region);
		routes[app.viewscreen_region->console] = routes[0];
	}
	if (app.gm_region)
		routes[app.gm_region->console] = resolve(*app.gm_region);
	if (app.console_regions.empty() && !app.viewscreen_region)
		routes[1] = routes[0];
	routes[31] = routes[0];
	const bool console =
	    !app.viewscreen_region &&
	    (app.screen == Screen::console || (app.screen == Screen::settings && app.settings_return == Screen::console));
	if (routes != app.screen_identities || device_console_mode != console)
	{
		app.screen_identities = std::move(routes);
		device_console_mode = console;
		restore_screen_gamepads(app);
	}
}
void NativeDisplayAdapter::poll(bool enabled)
{
	refresh_device_routes();
	if (enabled && (!app.session || !app.session->running()) && GetTime() >= next_display_refresh)
	{

		next_display_refresh = GetTime() + .5;
		if (phoenix::update_native_displays(app, enumerate_displays()))
		{
			std::cout << "Native displays refreshed: " << app.native_displays.size() << " attached\n";
			if (lobby_span)
				app.apply_displays();
		}
	}
}
} // namespace phoenix
