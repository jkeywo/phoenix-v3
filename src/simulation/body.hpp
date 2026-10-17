#pragma once
#include "math.hpp"
#include <phoenix/behavior.hpp>
#include <phoenix/world.hpp>
namespace phoenix {
struct Body {
    EntityId id = 0;
    std::string definition;
    fixed::Vec position, velocity, remainder;
    std::int64_t heading = 0, mass = 0, radius = 0, half_width = 0, half_length = 0;
    bool box = false;
};
struct Operation {
    std::int64_t speed = 0, heading = 0, acceleration_remainder = 0, turn_remainder = 0;
    bool engine_enabled = true, radar_enabled = true;
    TreeState tree;
};
void collide(Body& a, Body& b);
}
