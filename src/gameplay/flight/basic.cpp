module;
#include <algorithm>
#include <cstdint>
module phoenix.gameplay.flight;
namespace phoenix
{
void integrate_motion(Pose &pose, Motion &motion)
{
	motion.velocity.x = std::clamp(motion.velocity.x, -fixed::unit, fixed::unit);
	motion.velocity.y = std::clamp(motion.velocity.y, -fixed::unit, fixed::unit);
	motion.remainder = motion.remainder + motion.velocity;
	pose.position =
	    pose.position + fixed::Vec{motion.remainder.x / simulation_tick_hz, motion.remainder.y / simulation_tick_hz};
	motion.remainder.x %= simulation_tick_hz;
	motion.remainder.y %= simulation_tick_hz;
	pose.position.x = std::clamp(pose.position.x, -fixed::unit, fixed::unit);
	pose.position.y = std::clamp(pose.position.y, -fixed::unit, fixed::unit);
}
void step_basic_steering(Pose &pose, Steering &turn, std::int64_t effectiveness)
{
	auto available = turn.rate * effectiveness / 1000 + turn.remainder;
	auto step = available / simulation_tick_hz;
	turn.remainder = available % simulation_tick_hz;
	auto difference = (turn.heading - pose.heading + 540000) % 360000 - 180000;
	pose.heading = (pose.heading + std::clamp(difference, -step, step) + 360000) % 360000;
}
void step_basic_engine(const Pose &pose, Motion &motion, Engine &engine, std::int64_t effectiveness)
{
	engine.forward_output = 0;
	const auto direction = fixed::direction(pose.heading);
	const auto desired = fixed::scale(direction, engine.speed);
	damp_motion(motion, desired, engine.deceleration, engine.damping_remainder);
	if (!engine.enabled)
		return;
	const auto current = fixed::dot(motion.velocity, direction) / fixed::unit;
	const auto acceleration =
	    launch_acceleration(engine.acceleration, std::max<std::int64_t>(0, current), engine.max_speed);
	const auto available = acceleration * effectiveness / 1000 + engine.remainder;
	engine.remainder = available % simulation_tick_hz;
	const auto change = std::clamp<std::int64_t>(engine.speed - current, 0, available / simulation_tick_hz);
	motion.velocity = motion.velocity + fixed::scale(direction, change);
	engine.forward_output = static_cast<int>(std::clamp<std::int64_t>(
	    change * simulation_tick_hz * 1000 / std::max<std::int64_t>(1, engine.acceleration), 0, 1000));
}
} // namespace phoenix
