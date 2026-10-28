module;
#include <cstdint>
#include <raylib.h>
#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

export module phoenix.graphics.radar;
export import phoenix.content.catalog;

export import phoenix.graphics.assets;
export import phoenix.graphics.geometry;

export namespace phoenix
{
// Top-down geometry shared by runtime radars and tool maps. Heading zero points
// right; positive headings turn towards screen up.
struct RadarGlyph
{
	std::array<std::array<Vector2, 2>, 32> lines{};
	std::size_t count = 0;
	auto begin() const
	{
		return lines.begin();
	}
	auto end() const
	{
		return lines.begin() + count;
	}
};
RadarGlyph radar_icon_strokes(RadarIcon icon, Vector2 centre, float radius, float heading);
void draw_radar_icon(RadarIcon icon, Vector2 centre, float radius, float heading, Color colour);
void draw_radar_body(TextureCache &textures, const Catalog &catalog, const BodyView &body, Vector2 centre, float radius,
                     Color colour);

// Each radar owns one bounded GPU sample history.
class RadarSweep
{
public:
	RadarSweep();
	~RadarSweep();
	RadarSweep(const RadarSweep &) = delete;
	RadarSweep &operator=(const RadarSweep &) = delete;
	void draw(Vector2 centre, float radius, const SystemView &sensor, const Json &settings, double time,
	          bool reduced_motion = false);
	Json statistics() const;

private:
	struct State;
	std::unique_ptr<State> state_;
};

void draw_region_markers(const Json &regions, const std::function<Vector2(double, double)> &point, double scale,
                         const SystemView *radar = nullptr, Vector2 clip_centre = {}, float clip_radius = 0);
Color radar_selection_colour(const Catalog &, const std::string &source, const Json &tokens = Json::object());
void draw_radar_selection(Vector2 at, float radius, Color colour);
bool radar_contact_in_range(const BodyView &own, const BodyView &body, double range);
Vector2 radar_contact_point(const BodyView &own, const BodyView &body, double range, Vector2 centre, float radius);
void draw_radar_footprint(Vector2 at, float size, Vector2 centre, float radius, Color colour);
Vector2 bearing_ring_point(Vector2 centre, float radius, std::int64_t bearing);
void draw_bearing_ring(const Observation &, const SystemView &, Vector2 centre, float radius, EntityId selected = 0,
                       Color selection_colour = SKYBLUE);
void draw_radar_plot(TextureCache &, const Catalog &, const Observation &, const SystemView &, const Json &waypoint,
                     RadarSweep &, const Json &settings, Vector2 centre, float radius, double time,
                     bool reduced_motion);
void draw_navigation_plot(const BodyView &own, const Json &regions, const Json &objectives, const Json &waypoint,
                          const NavigationCamera &, Rectangle area, bool overlay = false, Color waypoint_colour = GOLD);
} // namespace phoenix
