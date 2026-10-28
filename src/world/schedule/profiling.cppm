module;
#include <chrono>
#include <map>
#include <string>
export module phoenix.world.schedule.profiling;
export namespace phoenix
{
// Explicit manual profiling only; timing never enters authoritative state.
class TickProfile
{
public:
	TickProfile();
	~TickProfile();
	void mark(const char *name);

private:
	using Clock = std::chrono::steady_clock;
	bool enabled;
	Clock::time_point previous;
	std::map<std::string, double> sample;
};
} // namespace phoenix
