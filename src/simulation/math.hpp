#pragma once
#include <cstdint>
#include <utility>
namespace phoenix::fixed {
constexpr std::int64_t unit = 1000000000;
struct Vec {
    std::int64_t x = 0, y = 0;
    Vec operator+(Vec b) const { return {x+b.x,y+b.y}; }
    Vec operator-(Vec b) const { return {x-b.x,y-b.y}; }
    Vec operator-() const { return {-x,-y}; }
};
inline std::int64_t dot(Vec a, Vec b) { return a.x*b.x+a.y*b.y; }
inline std::uint64_t sqrt(std::uint64_t value) {
    std::uint64_t result = 0, bit = std::uint64_t{1} << 62;
    while (bit > value) bit >>= 2;
    while (bit) { if (value >= result+bit) { value -= result+bit; result = (result>>1)+bit; } else result >>= 1; bit >>= 2; }
    return result;
}
inline std::int64_t length(Vec value) { return static_cast<std::int64_t>(sqrt(static_cast<std::uint64_t>(dot(value,value)))); }
inline Vec scale(Vec direction, std::int64_t magnitude) { return {direction.x*magnitude/unit,direction.y*magnitude/unit}; }
inline Vec normal(Vec value) { auto size = length(value); return size ? Vec{value.x*unit/size,value.y*unit/size} : Vec{unit,0}; }
// Integer CORDIC: no platform libm, floating point, or generated trig table.
// Heading 0 points along +X and increases counterclockwise in world space.
inline Vec direction(std::int64_t heading) {
    heading %= 360000; if (heading < 0) heading += 360000;
    if (heading == 0) return {unit,0}; if (heading == 90000) return {0,unit};
    if (heading == 180000) return {-unit,0}; if (heading == 270000) return {0,-unit};
    if (heading > 180000) heading -= 360000;
    int sign = 1;
    if (heading > 90000) { heading -= 180000; sign = -1; }
    else if (heading < -90000) { heading += 180000; sign = -1; }
    constexpr int angles[] = {45000,26565,14036,7125,3576,1790,895,448,224,112,56,28,14,7,3,2,1};
    std::int64_t x = 607252935, y = 0;
    for (int i = 0; i < 17; ++i) {
        auto old = x;
        if (heading >= 0) { x -= y >> i; y += old >> i; heading -= angles[i]; }
        else { x += y >> i; y -= old >> i; heading += angles[i]; }
    }
    return {sign*x,sign*y};
}
}
