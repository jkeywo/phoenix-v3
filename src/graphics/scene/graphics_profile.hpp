#pragma once
// Optional, manually enabled CPU timings for graphics submission stages.
namespace phoenix
{
class GraphicsTrace
{
	bool enabled_ = std::getenv("PHOENIX_PROFILE_GRAPHICS") != nullptr;
	double previous_ = enabled_ ? GetTime() : 0;

public:
	void mark(const char *name)
	{
		if (!enabled_)
			return;
		const double now = GetTime(), elapsed = (now - previous_) * 1000;
		previous_ = now;
		static std::map<std::string, std::vector<double>> samples;
		auto &values = samples[name];
		values.push_back(elapsed);
		if (values.size() == 200)
		{
			std::sort(values.begin(), values.end());
			double total = 0;
			for (const auto value : values)
				total += value;
			std::cout << "PHOENIX_GRAPHICS_PROFILE "
			          << Json{{"stage", name},
			                  {"mean", total / values.size()},
			                  {"p95", values[189]},
			                  {"max", values.back()}}
			                 .dump()
			          << std::endl;
			values.clear();
		}
	}
};
} // namespace phoenix
