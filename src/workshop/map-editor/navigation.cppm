module;
#include <imgui.h>
#include <raylib.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <map>

export module phoenix.workshop.map_navigation;
export import phoenix.contracts;
export import phoenix.ui.interaction;

export namespace phoenix
{
void draw_tool_radar_icon(ImDrawList *, RadarIcon, ImVec2 point, float heading, ImU32 colour, Texture2D sprite = {});
} // namespace phoenix
