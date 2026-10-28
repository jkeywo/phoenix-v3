module;
#include <cstdint>
#include <map>

module phoenix.gameplay.flight.work;

namespace phoenix
{
std::int64_t quote_funded_flight_work(const FlightDefinition &definition, const FlightState &state,
                                      const FlightDemand &demand, const SystemResourceGrant *grant,
                                      FlightWorkFunding funding)
{
	if (!funding.running || !state.enabled || !funding.available)
		return 0;
	const auto paid = funding.full ? 1000 : funding.paid;
	const auto output = grant ? grant->overdrive_permille : 1000;
	const auto native_work = quote_flight_work(definition, state, demand, paid, output);
	auto work = native_work.effort_milliticks + native_work.charging_milliticks;
	const auto charge = quote_flight_charge(definition, state);
	if (charge.progress && (funding.full || !charge.energy || (grant && grant->discrete_paid)) &&
	    (!grant || !grant->blocked) && (funding.full || funding.work_limit >= charge.progress))
		work += charge.progress;
	return work;
}
BasicSteeringWork BasicFlightWorkBook::steering(EntityId id, const Pose &initial, Steering effective, std::int64_t paid)
{
	auto &pose = poses_.try_emplace(id, initial).first->second;
	const auto active = effective.heading != pose.heading;
	BasicSteeringWork result{active ? 1000 : 0, active ? paid : 0, effective};
	step_basic_steering(pose, effective, paid);
	return result;
}
BasicEngineWork BasicFlightWorkBook::engine(EntityId id, const Pose &initial_pose, const Motion &initial_motion,
                                            Engine full, Engine actual, std::int64_t paid)
{
	auto &pose = poses_.try_emplace(id, initial_pose).first->second;
	auto &motion = motions_.try_emplace(id, initial_motion).first->second;
	auto full_motion = motion;
	step_basic_engine(pose, full_motion, full, 1000);
	const auto frozen = actual;
	step_basic_engine(pose, motion, actual, paid);
	return {full.forward_output, actual.forward_output, frozen};
}
} // namespace phoenix
