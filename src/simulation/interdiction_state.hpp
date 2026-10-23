#pragma once
namespace phoenix
{
struct InterdictionState
{
	std::vector<EntityId> ships;
	EntityId boarder = 0;
	std::string posture = "watch";
	std::uint64_t conversation = 0;
	std::int64_t progress = 0;
	bool attacked = false, reinforced = false, captured = false, blocked = false, finished = false;
	std::set<std::string> effects;
	std::vector<std::uint64_t> plea_messages;
	state_tree::Instance continuation;
};
} // namespace phoenix
