module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
#include <raylib.h>
#include <emscripten.h>
module phoenix.diagnostics.browser_profile;
// Manually enabled browser smoke evidence, never a machine-speed CI gate.
// Append &perf=1 to a runtime URL and reload to enable it.
// clang-format off: embedded JavaScript must retain its operators.
EM_JS(bool, phoenix_frame_profile_enabled, (), {
	return new URLSearchParams(location.search).get('perf') === '1';
});
EM_JS(double, phoenix_frame_profile_seconds, (), {
    return Math.min(600, Math.max(0, Number(new URLSearchParams(location.search).get('perf-seconds')) || 0));
});
EM_JS(void, phoenix_frame_profile_report, (const char* json), {
	const report = JSON.parse(UTF8ToString(json));
	report.visible = !document.hidden;
    report.heap_bytes = HEAPU8.length;
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

namespace phoenix
{
BrowserFrameProfile::BrowserFrameProfile(BackgroundProfile background)
    : enabled(phoenix_frame_profile_enabled()), capture_seconds(phoenix_frame_profile_seconds()),
      background_(background)
{
}
void BrowserFrameProfile::record(double started, double drawn, double ended, double session_work,
                                 const phoenix::Application &app)
{
	if (!enabled || complete)
		return;
	if (capture_seconds > 0 &&
	    (!app.session || !app.session->running() || !app.frame_metrics.contains("preparation") || warmup))
	{
		if (app.frame_metrics.contains("preparation") && warmup)
			--warmup;
		previous = 0;
		return;
	}
	if (!previous)
	{
		previous = started;
		window_started = started;
		first_tick = app.session ? app.session->ticks() : 0;
		return;
	}
	intervals.push_back((started - previous) * 1000);
	previous = started;
	++frames;
	totals["poll_tick"] += session_work * 1000;
	totals["draw"] += (drawn - started) * 1000;
	totals["end"] += (ended - drawn) * 1000;
	for (const auto &[name, seconds] : app.frame_metrics)
		totals["presentation." + name] += seconds * 1000;
	if (started - window_started < (capture_seconds > 0 ? capture_seconds : 5))
		return;
	for (auto &[name, milliseconds] : totals)
		milliseconds /= frames;
	std::sort(intervals.begin(), intervals.end());
	nlohmann::json report{
	    {"fps", frames / (started - window_started)},
	    {"frames", frames},
	    {"capture_seconds", started - window_started},
	    {"captured_ticks", app.session ? app.session->ticks() - first_tick : 0},
	    {"frame_p99_ms", intervals[static_cast<std::size_t>((intervals.size() - 1) * .99)]},
	    {"frame_p95_ms", intervals[static_cast<std::size_t>((intervals.size() - 1) * .95)]},
	    {"frame_max_ms", intervals.back()},
	    {"width", GetRenderWidth()},
	    {"height", GetRenderHeight()},
	    {"mean_ms", totals},
	    {"scope", "CPU wall time; frame interval includes browser scheduling; GPU not timed separately"}};
	if (background_)
		report["background"] = background_(app.session);
	phoenix_frame_profile_report(report.dump().c_str());
	complete = capture_seconds > 0;
	window_started = started;
	frames = 0;
	intervals.clear();
	totals.clear();
}
} // namespace phoenix
