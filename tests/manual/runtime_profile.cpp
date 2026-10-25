// Manual authored-content performance evidence, never a machine-speed CI gate.
#include <raylib.h>
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
import phoenix.application;
import phoenix.presentation;
import phoenix.session.execution;
using namespace phoenix;
Application app;
unsigned frames = 0, target = 600, warmup = 20;
double previous = 0, accumulated = 0, capture_started = 0, capture_seconds = 45;
double graphics_started = 0;
double discarded_seconds = 0, maximum_backlog = 0;
std::uint64_t capture_tick = 0;
unsigned focused_frames = 0;
std::map<std::string, std::vector<double>> samples;
std::string output;
bool graphics_ready = false;
bool probe_readouts = false, readout_probe_passed = false;
bool probe_accessibility = false, accessibility_probe_passed = false;
void profile_frame()
{
	const auto started = GetTime();
	const auto interval = previous ? started - previous : 0;
	previous = started;
	if (graphics_ready)
	{
		accumulated += std::min(interval, .25);
		if (!warmup)
		{
			discarded_seconds += std::max(0.0, interval - .25);
			maximum_backlog = std::max(maximum_backlog, accumulated);
		}
	}
	app.session->poll();
	unsigned ticks = 0;
	while (accumulated >= .05)
	{
		const auto tick_started = GetTime();
		app.session->tick();
		if (!warmup)
			samples["tick"].push_back((GetTime() - tick_started) * 1000);
		accumulated -= .05;
		++ticks;
	}
	const auto polled = GetTime();
	app.frame_metrics.clear();
	BeginDrawing();
	draw_screen(app);
	const auto drawn = GetTime();
	EndDrawing();
	const auto ended = GetTime();
	if (!graphics_ready)
	{
		graphics_ready = app.frame_metrics.contains("preparation");
		if (graphics_ready)
		{
			graphics_started = ended;
			std::cout << "PHOENIX_READY {}" << std::endl;
		}
		return;
	}
	if (warmup)
	{
		--warmup;
		return;
	}
	if (!capture_started)
	{
		capture_started = started;
		capture_tick = app.session->ticks() - ticks;
	}
	if (probe_readouts && !readout_probe_passed && frames >= 20)
		for (const auto &region : app.console_regions)
			readout_probe_passed |= probe_console_readouts(region.console);
#ifndef __EMSCRIPTEN__
	if (probe_accessibility && !accessibility_probe_passed && frames >= 20)
	{
		Application fixture;
		fixture.console_surface_active = fixture.native_menu = true;
		fixture.session = app.session;
		fixture.screen_readouts.emplace_back("probe.status", "Current readout");
		fixture.control_placements["probe.status"] = {0, 0, 100, 40, false, true};
		accessibility_probe_passed = probe_native_accessibility(fixture);
		publish_semantics(app);
	}
#endif
	if (IsWindowFocused())
		++focused_frames;
	samples["interval"].push_back(interval * 1000);
	samples["poll_tick"].push_back((polled - started) * 1000);
	samples["draw"].push_back((drawn - polled) * 1000);
	samples["end"].push_back((ended - drawn) * 1000);
	for (const auto &[name, value] : app.frame_metrics)
		samples[name].push_back(value * 1000);
	if (++frames < target && started - capture_started < capture_seconds)
		return;
	nlohmann::json report{{"frames", frames},
	                      {"ticks", app.session->ticks()},
	                      {"framebuffer", {GetRenderWidth(), GetRenderHeight()}},
	                      {"consoles", app.console_regions.size()},
	                      {"viewscreen", app.viewscreen_region.has_value()},
	                      {"capture_seconds", ended - capture_started},
	                      {"captured_ticks", app.session->ticks() - capture_tick},
	                      {"discarded_seconds", discarded_seconds},
	                      {"maximum_backlog_seconds", maximum_backlog},
	                      {"remaining_backlog_seconds", accumulated},
	                      {"focused_frames", focused_frames},
	                      {"warmup_seconds", capture_started - graphics_started},
	                      {"readout_probe_requested", probe_readouts},
	                      {"readout_probe_passed", readout_probe_passed},
	                      {"accessibility_probe_requested", probe_accessibility},
	                      {"accessibility_probe_passed", accessibility_probe_passed},
	                      {"units", "milliseconds"}};
	for (auto &[name, values] : samples)
	{
		std::sort(values.begin(), values.end());
		double sum = 0;
		for (const auto v : values)
			sum += v;
		report["stages"][name] = {{"n", values.size()},
		                          {"mean", sum / values.size()},
		                          {"p50", values[(values.size() - 1) / 2]},
		                          {"p95", values[static_cast<std::size_t>((values.size() - 1) * .95)]},
		                          {"p99", values[static_cast<std::size_t>((values.size() - 1) * .99)]},
		                          {"max", values.back()}};
	}
	std::cout << "PHOENIX_FRAME_PROFILE " << report.dump() << std::endl;
	target = frames;
#ifdef __EMSCRIPTEN__
	emscripten_cancel_main_loop();
	EM_ASM({ window.profileComplete = true; });
#else
	if (!output.empty())
	{
		std::ofstream file(output);
		file << report.dump(2);
	}
	TakeScreenshot((output + ".png").c_str());
#endif
}
int main(int argc, char **argv)
{
	try
	{
#ifdef __EMSCRIPTEN__
		setenv("PHOENIX_PROFILE_TICKS", "1", 1);
#else
		_putenv_s("PHOENIX_PROFILE_TICKS", "1");
#endif
		const std::string directory = argc > 1 ? argv[1] : "build/native";
		const std::string ship = argc > 2 ? argv[2] : "base.rescue-courier";
		const std::string layout = argc > 3 ? argv[3] : "console";
		target = argc > 4 ? std::stoul(argv[4]) : 600;
		const int width = argc > 5 ? std::stoi(argv[5]) : 1920, height = argc > 6 ? std::stoi(argv[6]) : 1080;
		output = argc > 7 ? argv[7] : "";
		capture_seconds = argc > 8 ? std::stod(argv[8]) : 45;
		const std::string seat = argc > 9 ? argv[9] : "helm";
		warmup = argc > 10 ? std::stoul(argv[10]) : 20;
		const std::string probes = argc > 11 ? argv[11] : "";
		probe_readouts = probes.find("readouts") != std::string::npos;
		probe_accessibility = probes.find("accessibility") != std::string::npos;
		if (probes.find("fonts") != std::string::npos)
		{
#ifdef __EMSCRIPTEN__
			setenv("PHOENIX_VERIFY_FONT_WIDTHS", "1", 1);
#else
			_putenv_s("PHOENIX_VERIFY_FONT_WIDTHS", "1");
#endif
		}
		app.can_host = true;
		app.session = make_local_session();
		app.session->add_pack(read_file(directory + "/base.zip"));
		app.session->start({"base.rescue-under-threat", {{"courier", ship}}, "courier", seat});
		const auto own = app.session->observation()->own.id;
		const bool cruiser = ship.find("cruiser") != std::string::npos;
		const std::vector<std::string> seats =
		    cruiser ? std::vector<std::string>{"helm", "engineering", "tactical", "comms", "science"}
		            : std::vector<std::string>{"helm", "support"};
		if (layout == "bridge")
		{
			app.session->release_console(1);
			app.viewscreen_region = ConsoleRegion{99, 0, 0, float(width / 3), float(height / 2)};
			for (std::size_t i = 0; i < seats.size(); ++i)
			{
				const auto route = i + 1;
				if (!app.session->claim_console(route, {own, seats[i]}))
					throw std::runtime_error("Cannot claim " + seats[i]);
				const auto cell = i + 1;
				app.console_regions.push_back({route, float((cell % 3) * (width / 3)), float((cell / 3) * (height / 2)),
				                               float(width / 3), float(height / 2)});
			}
		}
		else if (layout == "console")
			app.console_regions.push_back({1, 0, 0, float(width), float(height)});
		else
			app.viewscreen_region = ConsoleRegion{99, 0, 0, float(width), float(height)};
		app.screen = layout == "scene" ? Screen::viewscreen : Screen::console;
		open_window();
#ifndef __EMSCRIPTEN__
		if (layout == "bridge")
		{
			SetWindowState(FLAG_WINDOW_UNDECORATED);
			ClearWindowState(FLAG_WINDOW_RESIZABLE);
		}
#endif
		SetWindowSize(width, height);
		SetTargetFPS(0);
#ifdef __EMSCRIPTEN__
		emscripten_set_main_loop(profile_frame, 0, true);
#else
		while (frames < target && !close_requested())
			profile_frame();
		close_window();
		return frames == target ? 0 : 1;
#endif
	}
	catch (const std::exception &e)
	{
		std::cerr << "FAIL " << e.what() << std::endl;
		return 1;
	}
}
