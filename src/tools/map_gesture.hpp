// Presentation-only camera gestures. World positions remain under the fingers
// while the midpoint moves and the pinch span changes, including at zoom limits.
struct MapGesture
{
	struct Point
	{
		float x, y;
	};
	std::map<std::uint64_t, Point> contacts;
	template <class Pointer>
	void event(const Pointer &event, bool inside, float centre_x, float centre_y, float minimum, float maximum,
	           float &pan_x, float &pan_y, float &zoom)
	{
		if (event.phase == Pointer::down)
		{
			if (inside && contacts.size() < 2)
				contacts[event.id] = {event.x, event.y};
			return;
		}
		auto found = contacts.find(event.id);
		if (found == contacts.end())
			return;
		if (event.phase == Pointer::up || event.phase == Pointer::cancel)
		{
			contacts.erase(found);
			return;
		}
		auto midpoint = [&]()
		{
			Point p{};
			for (const auto &[id, point] : contacts)
			{
				p.x += point.x;
				p.y += point.y;
			}
			p.x /= contacts.size();
			p.y /= contacts.size();
			return p;
		};
		auto span = [&]()
		{
			if (contacts.size() != 2)
				return 0.f;
			const auto a = contacts.begin()->second, b = std::next(contacts.begin())->second;
			return std::hypot(a.x - b.x, a.y - b.y);
		};
		const auto before = midpoint();
		const auto old_span = span();
		found->second = {event.x, event.y};
		const auto after = midpoint();
		const auto new_zoom = old_span > 1 ? std::clamp(zoom * span() / old_span, minimum, maximum) : zoom;
		const auto ratio = new_zoom / zoom;
		pan_x = after.x - centre_x - (before.x - centre_x - pan_x) * ratio;
		pan_y = after.y - centre_y - (before.y - centre_y - pan_y) * ratio;
		zoom = new_zoom;
	}
};
