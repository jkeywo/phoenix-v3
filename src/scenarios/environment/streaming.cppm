module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <utility>
#include <vector>

export module phoenix.scenarios.environment.streaming;
export import phoenix.scenarios.environment;
export import phoenix.world.state;

export namespace phoenix
{
struct FieldRock
{
	SceneBody scene{};
	fixed::Vec velocity{}, remainder{};
	bool decorative = false, loaded = false;
};
struct AsteroidFocus
{
	fixed::Vec position;
	std::int64_t radius;
};
struct AsteroidWindow
{
	std::vector<std::array<std::int64_t, 4>> windows;
	std::set<std::pair<std::int64_t, std::int64_t>> cells;
	bool changed = false;
};
// Prepare and commit are separate so native installation still finishes before
// the derived window advances; failed installation cannot skip cells on retry.
class AsteroidWindowCache
{
public:
	AsteroidWindow prepare(std::span<const AsteroidFocus>, std::int64_t spacing) const;
	bool contains(std::int64_t x, std::int64_t y) const;
	void commit(AsteroidWindow);
	void clear();

private:
	std::vector<std::array<std::int64_t, 4>> windows_;
	std::set<std::pair<std::int64_t, std::int64_t>> cells_;
};
Json encode_asteroid_field(const FieldRock &);
FieldRock decode_asteroid_field(const Json &);
void validate_asteroid_field(const FieldRock &, std::span<const ScenarioRegion>, bool known_definition,
                             std::size_t sprite_entries, bool duplicate);
} // namespace phoenix
