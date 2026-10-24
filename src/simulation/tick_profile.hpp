#pragma once

// Explicit manual profiling only. Timing never enters authoritative state.
struct TickProfile
{
	using Clock = std::chrono::steady_clock;
	bool enabled = std::getenv("PHOENIX_PROFILE_TICKS") != nullptr;
	Clock::time_point previous = enabled ? Clock::now() : Clock::time_point{};
	std::map<std::string, double> sample;
	void mark(const char *name)
	{
		if (!enabled)
			return;
		const auto now = Clock::now();
		sample[name] = std::chrono::duration<double, std::milli>(now - previous).count();
		previous = now;
	}
	~TickProfile()
	{
		if (!enabled)
			return;
		static std::map<std::string, std::vector<double>> samples;
		static unsigned count = 0;
		for (const auto &[name, value] : sample)
			samples[name].push_back(value);
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
		std::cout << "PHOENIX_TICK_PROFILE " << report.dump() << std::endl;
	}
};
