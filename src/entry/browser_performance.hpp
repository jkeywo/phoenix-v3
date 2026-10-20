#pragma once

// Manually enabled browser smoke evidence, never a machine-speed CI gate.
// Append &perf=1 to a runtime URL and reload to enable it.
// clang-format off: embedded JavaScript must retain its operators.
EM_JS(bool, phoenix_frame_profile_enabled, (), {
	return new URLSearchParams(location.search).get('perf') === '1';
});
EM_JS(void, phoenix_frame_profile_report, (const char* json), {
	const report = JSON.parse(UTF8ToString(json));
	report.visible = !document.hidden;
	console.info('Phoenix frame profile', JSON.stringify(report));
	let output = document.getElementById('phoenix-frame-profile');
	if (!output) {
		output = document.createElement('output');
		output.id = 'phoenix-frame-profile';
		output.setAttribute('aria-label', 'Frame performance diagnostics');
		Object.assign(output.style, {
			position: 'fixed', right: '0', bottom: '0', zIndex: '10000',
			background: '#000e', color: '#fff', padding: '8px',
			font: '12px monospace', pointerEvents: 'none', whiteSpace: 'pre-wrap'
		});
		document.body.append(output);
	}
	const mean = report.mean_ms;
	output.textContent = `${report.fps.toFixed(1)} FPS | frame p95 ${report.frame_p95_ms.toFixed(1)} ms\n` +
		`poll/tick ${mean.poll_tick.toFixed(1)} | draw ${mean.draw.toFixed(1)} | end ${mean.end.toFixed(1)} ms\n` +
		`${report.width} x ${report.height} | ${report.visible ? 'visible' : 'hidden'}`;
});
// clang-format on

struct BrowserFrameProfile
{
	bool enabled = phoenix_frame_profile_enabled();
	double previous = 0, window_started = 0;
	unsigned frames = 0;
	std::vector<double> intervals;
	std::map<std::string, double> totals;

	void record(double started, double polled, double drawn, double ended, const phoenix::Application &app)
	{
		if (!enabled)
			return;
		if (!previous)
		{
			previous = started;
			window_started = started;
			return;
		}
		intervals.push_back((started - previous) * 1000);
		previous = started;
		++frames;
		totals["poll_tick"] += (polled - started) * 1000;
		totals["draw"] += (drawn - polled) * 1000;
		totals["end"] += (ended - drawn) * 1000;
		for (const auto &[name, seconds] : app.frame_metrics)
			totals["presentation." + name] += seconds * 1000;
		if (started - window_started < 5)
			return;
		for (auto &[name, milliseconds] : totals)
			milliseconds /= frames;
		std::sort(intervals.begin(), intervals.end());
		const nlohmann::json report{
		    {"fps", frames / (started - window_started)},
		    {"frames", frames},
		    {"frame_p95_ms", intervals[static_cast<std::size_t>((intervals.size() - 1) * .95)]},
		    {"frame_max_ms", intervals.back()},
		    {"width", GetRenderWidth()},
		    {"height", GetRenderHeight()},
		    {"mean_ms", totals},
		    {"scope", "CPU wall time; frame interval includes browser scheduling; GPU not timed separately"}};
		phoenix_frame_profile_report(report.dump().c_str());
		window_started = started;
		frames = 0;
		intervals.clear();
		totals.clear();
	}
};
