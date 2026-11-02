module;
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
module phoenix.data;
namespace phoenix
{
namespace
{
void emit(const char *stage, double milliseconds, std::uint64_t count) noexcept
{
	try
	{
		static std::mutex mutex;
		std::lock_guard lock(mutex);
		std::cout << "PHOENIX_STARTUP_STAGE "
		          << Json{{"stage", stage}, {"milliseconds", milliseconds}, {"count", count}}.dump() << std::endl;
	}
	catch (...)
	{
		// Diagnostics cannot turn successful loading into a gameplay failure.
	}
}
} // namespace
bool startup_profiling()
{
	static const bool enabled = std::getenv("PHOENIX_PROFILE_STARTUP") != nullptr;
	return enabled;
}
void startup_counter(const char *stage, std::uint64_t count)
{
	if (startup_profiling())
		emit(stage, 0, count);
}
StartupStage::StartupStage(const char *stage) : stage_(startup_profiling() ? stage : nullptr)
{
	if (stage_)
		start_ = std::chrono::steady_clock::now();
}
StartupStage::~StartupStage()
{
	if (stage_)
		emit(stage_, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count(), 1);
}
} // namespace phoenix
