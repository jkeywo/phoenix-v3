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

export module phoenix.diagnostics.browser_profile;
export import phoenix.application;
export namespace phoenix
{
struct BrowserFrameProfile
{
	using BackgroundProfile = Json (*)(const std::shared_ptr<SessionPort> &);
	explicit BrowserFrameProfile(BackgroundProfile background = nullptr);
	bool enabled = false;
	double capture_seconds = 0;
	unsigned warmup = 120;
	std::uint64_t first_tick = 0;
	bool complete = false;
	double previous = 0, window_started = 0;
	unsigned frames = 0;
	std::vector<double> intervals;
	std::map<std::string, double> totals;

	void record(double started, double drawn, double ended, double session_work, const phoenix::Application &app);

private:
	BackgroundProfile background_ = nullptr;
};
} // namespace phoenix
