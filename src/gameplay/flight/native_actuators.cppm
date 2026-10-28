module;
#include <flecs.h>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>
export module phoenix.gameplay.flight.native;
export import phoenix.gameplay.fitting;
export import phoenix.gameplay.flight.work;
export import phoenix.effects.machinery;

export namespace phoenix
{
struct FlightRestrictions
{
	bool impulse = false;
	int steering_limit = 1000;
};
// Borrowed actor-scoped reads and delivery only; no World or catalogue escape.
struct NativeFlightPorts
{
	std::function<std::int64_t(EntityId, const std::string &, PropertyKey, std::int64_t, std::int64_t)> property;
	std::function<bool(EntityId, const std::string &)> structurally_disabled;
	std::function<bool(EntityId)> docked;
	std::function<std::int64_t(EntityId, const std::string &)> propulsion_rate;
	std::function<std::span<const BodyView>(EntityId, const std::string &)> contacts;
	std::function<void(EntityId, const std::string &, std::size_t)> telemetry;
	std::function<std::uint64_t()> tick;
};
struct NativeFlightSnapshot
{
	std::vector<std::tuple<flecs::entity, FlightState, FlightDemand>> rows;
};
class NativeFlightActuators
{
public:
	NativeFlightActuators(flecs::world &, NativeFlightPorts);
	~NativeFlightActuators();
	NativeFlightActuators(const NativeFlightActuators &) = delete;
	NativeFlightActuators &operator=(const NativeFlightActuators &) = delete;
	void run_basic_steering(ProcessMachineryBook &);
	void run_basic_engines(ProcessMachineryBook &);
	void cancel_ship_impulse(ProcessMachineryBook &, flecs::entity);
	FlightRestrictions flight_restrictions(ProcessMachineryBook &, flecs::entity);
	void request_flight_resources(ProcessMachineryBook &, flecs::entity, FlightRestrictions);
	void reserve_paid_modifiers(ProcessMachineryBook &, flecs::entity, FlightRestrictions);
	void prepare_paid_modifiers(ProcessMachineryBook &, flecs::entity, FlightRestrictions,
	                            const ShipResourceAllocation *);
	void run_equipment_actuation(ProcessMachineryBook &, flecs::entity, FlightRestrictions);
	void quote_basic_process_work(ProcessMachineryBook &);
	NativeFlightSnapshot snapshot(std::span<const flecs::entity>) const;
	void restore(NativeFlightSnapshot &);

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace phoenix
