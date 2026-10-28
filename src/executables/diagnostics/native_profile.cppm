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

export module phoenix.diagnostics.native_profile;
export import phoenix.application;
export namespace phoenix
{
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
	bool record(double simulation, double drawing, double total, const Application &app);
};
} // namespace phoenix
