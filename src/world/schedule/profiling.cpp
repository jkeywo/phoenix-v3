module;
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
module phoenix.world.schedule.profiling;
namespace phoenix
{
thread_local TickProfile *TickProfile::active = nullptr;
TickProfile::TickProfile()
    : enabled(std::getenv("PHOENIX_PROFILE_TICKS") != nullptr), previous(enabled ? Clock::now() : Clock::time_point{})
{
	parent = active;
	active = enabled ? this : nullptr;
}
TickWorkProfile::TickWorkProfile(const char *label) : owner(TickProfile::active), name(label)
{
	if (owner)
		started = std::chrono::steady_clock::now();
}
TickWorkProfile::~TickWorkProfile()
{
	if (!owner)
		return;
	const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
	auto found = owner->work.find(name);
	if (found == owner->work.end())
		found = owner->work.emplace(name, TickProfile::WorkSample{}).first;
	++found->second.calls;
	found->second.milliseconds += elapsed;
}
void TickProfile::mark(const char *name)
{
	if (!enabled)
		return;
	const auto now = Clock::now();
	sample[name] = std::chrono::duration<double, std::milli>(now - previous).count();
	previous = now;
}
TickProfile::~TickProfile()
{
	active = parent;
	if (!enabled)
		return;
	mark("tick_tail");
	static thread_local std::map<std::string, std::vector<double>> samples;
	static thread_local std::map<std::string, WorkSample> work_samples;
	static thread_local unsigned count = 0;
	for (const auto &[name, value] : sample)
		samples[name].push_back(value);
	for (const auto &[name, value] : work)
	{
		auto &total = work_samples[name];
		total.calls += value.calls;
		total.milliseconds += value.milliseconds;
	}
	if (++count % 200)
		return;
	nlohmann::json report{{"ticks", count}, {"units", "milliseconds"}};
	for (auto &[name, values] : samples)
	{
		std::sort(values.begin(), values.end());
		double sum = 0;
		for (const auto value : values)
			sum += value;
		report["stages"][name] = {{"mean", sum / values.size()},
		                          {"p50", values[(values.size() - 1) / 2]},
		                          {"p95", values[static_cast<std::size_t>((values.size() - 1) * .95)]},
		                          {"max", values.back()}};
		values.clear();
	}
	for (const auto &[name, value] : work_samples)
		report["work_inclusive"][name] = {{"calls", value.calls},
		                                  {"mean_ms_per_tick", value.milliseconds / 200},
		                                  {"mean_ms_per_call", value.milliseconds / value.calls}};
	work_samples.clear();
	std::cout << "PHOENIX_TICK_PROFILE " << report.dump() << std::endl;
}
} // namespace phoenix
