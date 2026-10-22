#pragma once
namespace phoenix
{
// Numeric continuation on fitted apparatus. No presentation or JSON work in the tick loop.
struct PsychicGhostShape
{
	std::int64_t x = 0, y = 0, vx = 0, vy = 0, mx = 0, my = 0, heading = 0, altitude = 0;
	std::int64_t acceleration_remainder = 0, turn_remainder = 0;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PsychicGhostShape, x, y, vx, vy, mx, my, heading, altitude, acceleration_remainder,
                                   turn_remainder)
struct PsychicSeatState
{
	std::string officer;
	EntityId target = 0;
	std::int64_t aim_x = 0, aim_y = 0;
	bool active = false;
	std::uint64_t activation = 0, ready = 0;
	// Derived funded work; never restored as authority.
	std::int64_t work = 0;
	std::vector<PsychicGhostShape> shapes;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PsychicSeatState, officer, target, aim_x, aim_y, active, activation, ready, shapes)
struct PsychicChoirState
{
	std::vector<PsychicSeatState> seats;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(PsychicChoirState, seats)
struct GhostReception
{
	EntityId id = 0, observer = 0, source = 0;
	std::string choir, power;
	std::size_t seat = 0, member = 0;
	std::uint64_t activation = 0, seen = 0;
	std::int64_t ended = -1, scan = 0;
	bool dispelled = false;
	PsychicGhostShape last;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(GhostReception, id, observer, source, choir, power, seat, member, activation, seen,
                                   ended, scan, dispelled, last)
} // namespace phoenix
