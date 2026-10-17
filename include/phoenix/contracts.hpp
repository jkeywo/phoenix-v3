#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace phoenix {
using EntityId = std::uint64_t;
struct BodyView { EntityId id; std::string definition; std::int64_t x, y, vx, vy, heading; };
struct Observation { BodyView own; std::vector<BodyView> contacts; };
struct OperatingCommand { EntityId target; std::string action; std::int64_t value; };
}
