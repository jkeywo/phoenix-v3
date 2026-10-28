module;
#include <cstdint>
#include <map>

export module phoenix.gameplay.flight.work;
export import phoenix.gameplay.flight;
export import phoenix.resources.equipment;

export namespace phoenix
{
struct FlightWorkFunding
{
	bool running = true, available = true, full = false;
	std::int64_t paid = 1000, work_limit = resource_limit;
};
std::int64_t quote_funded_flight_work(const FlightDefinition &, const FlightState &, const FlightDemand &,
                                      const SystemResourceGrant *, FlightWorkFunding);
struct BasicSteeringWork
{
	std::int64_t full = 0, available = 0;
	Steering frozen;
};
struct BasicEngineWork
{
	std::int64_t full = 0, available = 0;
	Engine frozen;
};
// Candidate physics is shared by every mechanism on one native body. The book
// retains the original steering-before-engine simulation order.
class BasicFlightWorkBook
{
public:
	BasicSteeringWork steering(EntityId, const Pose &, Steering, std::int64_t paid);
	BasicEngineWork engine(EntityId, const Pose &, const Motion &, Engine full, Engine actual, std::int64_t paid);

private:
	std::map<EntityId, Pose> poses_;
	std::map<EntityId, Motion> motions_;
};
} // namespace phoenix
