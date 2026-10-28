module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

export module phoenix.world.spatial;
export import phoenix.world.state;

export namespace phoenix
{
struct Bounds
{
	std::int64_t min_x, min_y, max_x, max_y;
	inline bool operator==(const Bounds &) const = default;
	inline bool overlaps(const Bounds &other) const
	{
		return min_x <= other.max_x && max_x >= other.min_x && min_y <= other.max_y && max_y >= other.min_y;
	}
	static inline Bounds around(fixed::Vec position, std::int64_t extent)
	{
		return {position.x - extent, position.y - extent, position.x + extent, position.y + extent};
	}
	static inline Bounds merge(const Bounds &a, const Bounds &b)
	{
		return {std::min(a.min_x, b.min_x), std::min(a.min_y, b.min_y), std::max(a.max_x, b.max_x),
		        std::max(a.max_y, b.max_y)};
	}
};

// Flat, balanced bounds tree. Keys are caller-owned row indices; no component
// pointers, coordinate-sized grids or per-query heap allocations are needed.
class SpatialIndex
{
public:
	static inline constexpr std::size_t none = static_cast<std::size_t>(-1);
	inline void rebuild(std::span<const Bounds> bounds)
	{
		nodes_.clear();
		nodes_.reserve(bounds.size() * 2);
		leaves_.resize(bounds.size());
		order_.resize(bounds.size());
		for (std::size_t i = 0; i < order_.size(); ++i)
			order_[i] = i;
		root_ = bounds.empty() ? none : build(bounds, 0, bounds.size(), none);
	}
	inline void update(std::size_t key, Bounds bounds)
	{
		auto node = leaves_[key];
		if (nodes_[node].bounds == bounds)
			return;
		nodes_[node].bounds = bounds;
		for (node = nodes_[node].parent; node != none; node = nodes_[node].parent)
		{
			auto combined = Bounds::merge(nodes_[nodes_[node].left].bounds, nodes_[nodes_[node].right].bounds);
			if (nodes_[node].bounds == combined)
				break;
			nodes_[node].bounds = combined;
		}
	}
	// Return the lowest overlapping key at or after begin. This preserves a
	// sequential solver's pair order without generating a stale candidate list.
	inline std::size_t first(const Bounds &bounds, std::size_t begin) const
	{
		auto best = none;
		if (root_ != none)
			find_first(root_, bounds, begin, best);
		return best;
	}
	template <typename Visitor> inline void each(const Bounds &bounds, Visitor &&visitor) const
	{
		if (root_ != none)
			visit(root_, bounds, visitor);
	}

private:
	struct Node
	{
		Bounds bounds;
		std::size_t parent, left = none, right = none, minimum, maximum;
	};
	std::vector<Node> nodes_;
	std::vector<std::size_t> leaves_, order_;
	std::size_t root_ = none;
	inline std::size_t build(std::span<const Bounds> bounds, std::size_t begin, std::size_t end, std::size_t parent)
	{
		auto index = nodes_.size();
		auto key = order_[begin];
		nodes_.push_back({bounds[key], parent, none, none, key, key});
		if (end - begin == 1)
		{
			leaves_[key] = index;
			return index;
		}
		auto enclosing = bounds[key];
		for (auto i = begin + 1; i < end; ++i)
			enclosing = Bounds::merge(enclosing, bounds[order_[i]]);
		bool x_axis = enclosing.max_x - enclosing.min_x >= enclosing.max_y - enclosing.min_y;
		auto middle = begin + (end - begin) / 2;
		std::nth_element(order_.begin() + begin, order_.begin() + middle, order_.begin() + end,
		                 [&](std::size_t a, std::size_t b)
		                 {
			                 auto ac = x_axis ? bounds[a].min_x + bounds[a].max_x : bounds[a].min_y + bounds[a].max_y;
			                 auto bc = x_axis ? bounds[b].min_x + bounds[b].max_x : bounds[b].min_y + bounds[b].max_y;
			                 return ac == bc ? a < b : ac < bc;
		                 });
		auto left = build(bounds, begin, middle, index);
		auto right = build(bounds, middle, end, index);
		nodes_[index] = {enclosing,
		                 parent,
		                 left,
		                 right,
		                 std::min(nodes_[left].minimum, nodes_[right].minimum),
		                 std::max(nodes_[left].maximum, nodes_[right].maximum)};
		return index;
	}
	inline void find_first(std::size_t index, const Bounds &bounds, std::size_t begin, std::size_t &best) const
	{
		const auto &node = nodes_[index];
		if (node.maximum < begin || node.minimum >= best || !node.bounds.overlaps(bounds))
			return;
		if (node.left == none)
		{
			best = node.minimum;
			return;
		}
		auto first = node.left, second = node.right;
		if (nodes_[first].minimum > nodes_[second].minimum)
			std::swap(first, second);
		find_first(first, bounds, begin, best);
		find_first(second, bounds, begin, best);
	}
	template <typename Visitor> inline void visit(std::size_t index, const Bounds &bounds, Visitor &visitor) const
	{
		const auto &node = nodes_[index];
		if (!node.bounds.overlaps(bounds))
			return;
		if (node.left == none)
			visitor(node.minimum);
		else
		{
			visit(node.left, bounds, visitor);
			visit(node.right, bounds, visitor);
		}
	}
};
void collide(CollisionBody &a, CollisionBody &b);
// Sweep the actual circle/rotated-box shapes over relative translation. On a
// hit, stop both at first contact and apply the ordinary collision response.
std::optional<std::int64_t> sweep_contact_time(const CollisionBody &a, const CollisionBody &b, fixed::Vec start_a,
                                               fixed::Vec start_b, bool spatial = false);
bool sweep_collide(CollisionBody &a, CollisionBody &b, fixed::Vec start_a, fixed::Vec start_b, bool spatial = false);
} // namespace phoenix
