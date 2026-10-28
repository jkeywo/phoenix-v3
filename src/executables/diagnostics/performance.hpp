#pragma once
#include <algorithm>
#include <array>
#include <vector>
namespace phoenix
{
// Manual smoke evidence. No machine-specific performance threshold in CI.
struct FrameCapture
{
	std::vector<std::array<double, 3>> samples;
	std::map<std::string, std::vector<double>> stages;
	unsigned focused_frames = 0;
	bool completed = false;
	unsigned warmup = 120, target = 1800;
	double seconds = 0, started = 0;
	std::uint64_t started_tick = 0;
	std::string output;
	bool record(double simulation, double drawing, double total, const Application &app)
	{
		if (output.empty())
			return false;
		if (warmup)
		{
			--warmup;
			return false;
		}
		if (!started)
		{
			started = GetTime();
			started_tick = app.session->ticks();
		}
		samples.push_back({simulation * 1000, drawing * 1000, total * 1000});
		if (IsWindowFocused())
			++focused_frames;
		for (const auto &[name, duration] : app.frame_metrics)
			stages[name].push_back(duration * 1000);
		if (seconds > 0 ? GetTime() - started < seconds : samples.size() < target)
			return false;
		Json result{
		    {"schema", 1},
		    {"samples", samples.size()},
		    {"capture_seconds", GetTime() - started},
		    {"captured_ticks", app.session->ticks() - started_tick},
		    {"units", "milliseconds"},
		    {"framebuffer", {GetRenderWidth(), GetRenderHeight()}},
		    {"configured_consoles", app.console_regions.size()},
		    {"viewscreen", app.viewscreen_region.has_value()},
		    {"simulation_tick", app.session->ticks()},
		    {"focused_frames", focused_frames},
		    {"timing_scope", "CPU wall time; total includes presentation/frame limiting; GPU not timed separately"}};
		result["background"] = frame_session_profile(app.session);
		unsigned active = 0;
		for (const auto &region : app.console_regions)
			if (app.session->console_observation(region.console))
				++active;
		result["active_consoles"] = active;
		for (auto &[name, values] : stages)
		{
			std::sort(values.begin(), values.end());
			result["presentation_stages"][name] = {{"p50", values[(values.size() - 1) / 2]},
			                                       {"p95", values[static_cast<std::size_t>((values.size() - 1) * .95)]},
			                                       {"max", values.back()}};
		}
		for (unsigned index = 0; index < 3; ++index)
		{
			std::vector<double> values;
			for (const auto &sample : samples)
				values.push_back(sample[index]);
			std::sort(values.begin(), values.end());
			auto percentile = [&](double p)
			{
				return values[static_cast<std::size_t>((values.size() - 1) * p)];
			};
			double sum = 0;
			for (const auto value : values)
				sum += value;
			const double mean = sum / values.size();
			if (index == 2)
				result["fps"] = 1000 / mean;
			result[index == 0   ? "simulation_and_poll"
			       : index == 1 ? "draw_submission"
			                    : "frame"] = {{"mean", mean},
			                                  {"p50", percentile(.5)},
			                                  {"p95", percentile(.95)},
			                                  {"p99", percentile(.99)},
			                                  {"max", values.back()}};
		}
		write_file(output, json_bytes(result));
		auto image = LoadImageFromScreen();
		const bool captured = ExportImage(image, (output + ".png").c_str());
		UnloadImage(image);
		if (!captured)
			throw std::runtime_error("Cannot export the performance framebuffer capture");
		std::cout << "Manual performance capture: " << result.dump() << '\n';
		if (active != app.console_regions.size())
			throw std::runtime_error("Performance smoke has vacant Consoles");
		completed = true;
		return true;
	}
};
} // namespace phoenix
