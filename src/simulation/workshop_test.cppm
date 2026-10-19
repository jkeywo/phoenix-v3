module;
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

export module phoenix.workshop_test;
export import phoenix.simulation;

export namespace phoenix
{
enum class WorkshopTestAction : std::uint8_t
{
	Start,
	Pause,
	Step,
	Restart,
	Stop
};
struct WorkshopTestRequest
{
	Bytes pack;
	std::vector<Bytes> dependencies;
	std::string mission, slot, ship;
	WorkshopTestAction action = WorkshopTestAction::Start;
};
class GameplayTestHost
{
public:
	GameplayTestHost();
	~GameplayTestHost();
	GameplayTestHost(const GameplayTestHost &) = delete;
	bool submit(WorkshopTestRequest request);
	void advance(std::uint32_t elapsed_ms);
	Json feedback() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace phoenix
