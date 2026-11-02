module;
#include <chrono>
#include <cstdint>
#include <functional>
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
	friend class TickWorkProfile;
	using Clock = std::chrono::steady_clock;
	struct WorkSample
	{
		std::uint64_t calls = 0;
		double milliseconds = 0;
	};
	static thread_local TickProfile *active;
	TickProfile *parent = nullptr;
	bool enabled;
	Clock::time_point previous;
	std::map<std::string, double> sample;
	std::map<std::string, WorkSample, std::less<>> work;
};
// Nested CPU timings are inclusive, and never affect gameplay scheduling.
class TickWorkProfile
{
public:
	explicit TickWorkProfile(const char *name);
	~TickWorkProfile();
	TickWorkProfile(const TickWorkProfile &) = delete;
	TickWorkProfile &operator=(const TickWorkProfile &) = delete;

private:
	TickProfile *owner;
	const char *name;
	std::chrono::steady_clock::time_point started;
};
} // namespace phoenix
