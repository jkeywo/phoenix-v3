#include <raylib.h>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

import phoenix.navigation;
import phoenix.presentation;
import phoenix.tools;

#include "native_smokes.hpp"
#include "controller_ui.hpp"
#include "bridge_ui.hpp"

void NativeSmoke::prepare(phoenix::Application &app)
{
	if (gui && app.session->running())
		for (const auto &region : app.console_regions)
			if (!app.session->console_observation(region.console))
				for (const auto &seat : app.session->console_seats())
					if (seat.available && app.session->claim_console(region.console, seat.address))
						break;

	if (ai_config && !ai_config_opened && phoenix::ai_configuration_available(app) && ++ai_config_ready_frames >= 120)
	{
		TakeScreenshot("build/ai-button.png");
		phoenix::invoke_navigation(app, "ai.config");
		ai_config_opened = true;
		frames = 60;
	}
	if (ai_config && !ai_config_opened && GetTime() - started > 30)
		throw std::runtime_error("AI configuration smoke pairing timed out");
}
NativeSmokeResult NativeSmoke::frame(phoenix::Application &app)
{
	if (menu.enabled)
	{
		try
		{
			if (menu.frame(app))
				return NativeSmokeResult::Stop;
		}
		catch (const std::exception &error)
		{
			std::cerr << error.what() << '\n';
			phoenix::end_tools();
			phoenix::close_window();
			return NativeSmokeResult::Failed;
		}
		if (GetTime() - started > (menu.intro ? 150 : menu.assign_displays ? 90 : 60))
		{
			std::cerr << "Menu smoke timed out at stage " << menu.stage << " / tick " << app.session->ticks() << '\n';
			phoenix::end_tools();
			phoenix::close_window();
			return NativeSmokeResult::Failed;
		}
	}
	if (join)
	{
		if (!join_submitted)
		{
			phoenix::invoke_navigation(app, "game.connect-client");
			join_submitted = true;
		}
		if (!claim_submitted)
			for (const auto &[id, label] : app.session->lobby_controls())
				if (id == "network.claim")
				{
					phoenix::invoke_navigation(app, id);
					claim_submitted = true;
				}
		if (app.session->observation() && app.screen == phoenix::Screen::console)
			++join_ready_frames;
		else
			join_ready_frames = 0;
		if (join_ready_frames >= 3)
		{
			TakeScreenshot("network-console-smoke.png");
			std::cout << "Native join form, station claim and permitted console: PASS\n";
			return NativeSmokeResult::Stop;
		}
		if (GetTime() - started > 20)
		{
			std::cerr << "Native join smoke failed: " << app.session->connection_status() << '\n';
			phoenix::end_tools();
			phoenix::close_window();
			return NativeSmokeResult::Failed;
		}
	}
	if (qr && frames == 6)
		phoenix::invoke_navigation(app, "game.qr-client");
	if (gui)
	{
		for (const auto [frame, stage] :
		     std::vector<std::pair<int, unsigned>>{{85, 0}, {70, 1}, {55, 2}, {40, 3}, {25, 4}})
			if (frames == frame && !phoenix::probe_gui_navigation(app, stage))
				throw std::runtime_error("GUI controller smoke failed at stage " + std::to_string(stage));
		if (frames == 3)
			TakeScreenshot("build/gui-controller-smoke.png");
		if (frames == 1)
			std::cout
			    << "GUI controller traversal, numeric commit/cancel, Settings, text scale and mode smoke passed\n";
	}
	if (tools_presentation)
	{
		for (const auto [frame, scale] :
		     std::vector<std::pair<int, int>>{{95, 75}, {80, 100}, {65, 150}, {50, 200}, {35, 100}})
			if (frames == frame)
			{
				app.tools_scale = scale;
				app.pseudo_locale = scale >= 150;
			}
		if (frames == 48)
			app.screen = phoenix::Screen::gm;
		for (int frame : {90, 75, 60, 45, 30})
			if (frames == frame)
			{
				if (!phoenix::probe_tool_presentation(app))
					throw std::runtime_error("Tool presentation smoke failed");
				TakeScreenshot(("build/gui-tools-" + std::to_string(app.tools_scale) +
				                (app.screen == phoenix::Screen::gm ? "-gm" : "") + ".png")
				                   .c_str());
			}
		if (frames == 1)
			std::cout << "Workshop/GM font, scale reset, pseudo-localisation and literal parameters smoke passed\n";
	}

	if (readout && frames == 1 && !phoenix::probe_console_readouts(1))
	{
		std::cerr << "Deferred readout smoke failed or found no changed offscreen readout\n";
		return NativeSmokeResult::Failed;
	}
	if (helm)
	{
		if (frames == 80)
		{
			helm_tick = app.session->ticks();
			if (!phoenix::probe_console_helm(app, 1))
				throw std::runtime_error("Helm pointer probe failed");
		}
		if (frames == 60)
		{
			if (!phoenix::probe_console_helm(app, 1, true))
				throw std::runtime_error("Helm per-channel consumption probe failed");
			phoenix::invoke_navigation(app, "console/1/settings");
		}
		if (frames == 55)
		{
			if (app.screen != phoenix::Screen::console ||
			    !phoenix::invoke_navigation(app, "console/1/local-settings.tab/session"))
				throw std::runtime_error("Settings left the native station");
		}
		if (frames == 50 && !phoenix::invoke_navigation(app, "console/1/game.control/61776179"))
			throw std::runtime_error("Settings Away action failed");
		if (frames == 30 && !phoenix::invoke_navigation(app, "console/1/game.control/61776179"))
			throw std::runtime_error("Settings resume action failed");
		if (frames == 20)
			phoenix::invoke_navigation(app, "console/1/local-settings.close");
		if (frames == 1)
		{
			if (app.session->ticks() <= helm_tick)
				throw std::runtime_error("Helm input stalled simulation");
			std::cout << "Helm smoke passed: direct throttle, heading, Settings Away/resume and advancing ticks\n";
		}
	}
	if (controller_report)
		probe_controller_reports(app, frames);
	if (controller_workshop)
	{
		for (const auto [frame, stage] :
		     std::vector<std::pair<int, unsigned>>{{175, 0}, {155, 1}, {100, 2}, {80, 3}, {60, 4}, {20, 5}})
			if (frames == frame && !phoenix::probe_workshop_controllers(stage))
				throw std::runtime_error("Workshop Controller smoke failed at stage " + std::to_string(stage));
		if (frames == 1)
			std::cout << "Workshop Controller edit/export/reopen/Test/Pause/Step/Restart smoke passed\n";
	}

	if (personnel)
	{
		if (frames == 80)
		{
			std::string open;
			for (const auto &[id, label] : app.screen_controls)
				if (id.ends_with("/personnel"))
					open = id;
			if (open.empty() && GetTime() - started < 90)
			{
				frames = 81;
				return NativeSmokeResult::Repeat;
			}
			if (open.empty() || !phoenix::invoke_navigation(app, open))
				throw std::runtime_error("Personnel button missing after loading");
		}
		if (frames == 60 || frames == 40 || frames == 20)
		{
			const auto prefix = frames == 60   ? "console/1/personnel/select/"
			                    : frames == 40 ? "console/1/personnel/relieve"
			                                   : "console/1/personnel/assign/";
			std::string action;
			for (const auto &[id, label] : app.screen_controls)
				if (id.starts_with(prefix))
				{
					action = id;
					break;
				}
			if (action.empty() || !phoenix::invoke_navigation(app, action))
				throw std::runtime_error("Personnel roster action missing");
		}
		if (frames == 2)
			TakeScreenshot("build/personnel-smoke.png");
	}
	if (engineering)
	{
		if ((frames == 80 && !phoenix::probe_console_engineering(1, 0)) ||
		    (frames == 50 && !phoenix::probe_console_engineering(1, 1)) ||
		    (frames == 20 && !phoenix::probe_console_engineering(1, 2)))
			throw std::runtime_error("Engineering telemetry/control probe failed");
	}
	if (on_screen)
	{
		const auto press = [&](const std::string &action)
		{
			if (!phoenix::invoke_navigation(app, "console/1/" + action))
				throw std::runtime_error("On screen smoke action refused: " + action);
		};
		if (frames == 130 || frames == 80 || frames == 30)
		{
			std::string action;
			for (const auto &[id, label] : app.screen_controls)
				if (id.starts_with("console/1/on-screen/"))
				{
					action = id.substr(10);
					break;
				}
			if (action.empty())
				throw std::runtime_error("On screen button absent from source Console");
			if (!phoenix::probe_console_on_screen(1, action.substr(10)))
				throw std::runtime_error("On screen pointer hit test failed");
		}
		if (frames == 90)
			press("game.console/navigation");
		if (frames == 60)
			press("nav.in");
		if (frames == 50)
			press("nav.right");
		if (frames == 100 || frames == 40 || frames == 10)
		{
			const auto board = app.session->public_board();
			const std::string kind =
			    board.contains("display") ? board.at("display").at("kind").get<std::string>() : "normal";
			const auto expected = frames == 100 ? "radar" : frames == 40 ? "navigation" : "normal";
			if (kind != expected)
				throw std::runtime_error("On screen smoke unexpected display: " + kind);
			TakeScreenshot(("build/on-screen-" + kind + ".png").c_str());
			std::cout << "On screen smoke: " << kind << "\n";
		}
	}
	if (navigation)
	{
		const auto action = frames == 45   ? "game.console/navigation"
		                    : frames == 35 ? "nav.set"
		                    : frames == 30 ? "nav.in"
		                    : frames == 25 ? "nav.right"
		                    : frames == 20 ? "nav.clear"
		                    : frames == 15 ? "nav.reset"
		                                   : "";
		if (*action && !phoenix::invoke_navigation(app, std::string("console/1/") + action))
			throw std::runtime_error(std::string("Navigation smoke action failed: ") + action);
	}
	if (bridge_width)
	{
		try
		{
			probe_bridge_ui(app, frames, bridge_width, bridge_height);
		}
		catch (const std::exception &error)
		{
			std::cerr << "Bridge smoke: " << error.what() << std::endl;
			phoenix::end_tools();
			phoenix::close_window();
			return NativeSmokeResult::Failed;
		}
	}
	if (frames && --frames == 0)
	{
		TakeScreenshot(ai_config_opened                            ? "build/ai-configuration.png"
		               : qr                                        ? "invitation-smoke.png"
		               : workshop                                  ? "workshop-smoke.png"
		               : app.screen == phoenix::Screen::console    ? "console-smoke.png"
		               : app.screen == phoenix::Screen::viewscreen ? "viewscreen-smoke.png"
		                                                           : "home-smoke.png");
		return NativeSmokeResult::Stop;
	}
	return NativeSmokeResult::Next;
}
