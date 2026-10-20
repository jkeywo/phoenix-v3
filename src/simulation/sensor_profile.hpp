// Included inside World::Impl. Only derived reach intervals leave the authority.
std::int64_t sector_reach(fixed::Vec origin, std::int64_t range, std::int64_t begin, std::int64_t end,
                          bool conservative) const
{
	const auto direction = fixed::direction((begin + end) / 2);
	auto visible = [&](std::int64_t distance)
	{
		if (!distance)
			return true;
		const auto target = origin + fixed::scale(direction, distance);
		// Arc length bounds chord displacement for every ray in the sector.
		// 355/113 rounds pi upwards. Extra 32 mdeg bounds CORDIC angle rounding;
		// 16 mm covers endpoint, slab and path-length integer truncation.

		if (std::abs(target.x) > scenario_coordinate_limit || std::abs(target.y) > scenario_coordinate_limit)
			return false;
		const auto measured = conservative ? distance + 2 : std::max<std::int64_t>(1, distance - 2);
		if (measured > range)
			return false;
		const auto budget = (range - measured) * 1000000 / measured;
		std::int64_t depth = 0;
		for (const auto &region : regions)
		{
			if (!region.nebula || !region.nebula->density || !region.nebula->sensor_factor)
				continue;
			// Only the portion of a ray inside this footprint contributes.
			// Its distance from the observer cannot exceed this bounding sphere.
			const auto extent =
			    region.shape == ScenarioRegionShape::Rectangle ? region.half_width + region.half_height : region.radius;
			const auto relevant = std::min(
			    distance, fixed::length({region.centre.x - origin.x, region.centre.y - origin.y}) + extent + 32);
			const auto padding = (relevant * (end - begin + 32) * 355 + 113 * 360000 - 1) / (113 * 360000) + 16;
			ScenarioRegion shape;
			shape.shape = region.shape;
			shape.centre = region.centre;
			const auto offset = conservative ? padding : -padding;
			shape.radius = region.radius + offset;
			shape.half_width = region.half_width + offset;
			shape.half_height = region.half_height + offset;
			shape.inner_radius = std::max<std::int64_t>(0, region.inner_radius - offset);
			if (shape.shape == ScenarioRegionShape::Rectangle ? (shape.half_width <= 0 || shape.half_height <= 0)
			                                                  : shape.radius <= 0)
				continue;
			if (shape.shape == ScenarioRegionShape::Annulus && shape.inner_radius >= shape.radius)
				continue;
			auto path = scenario_path_length(shape, {origin.x, origin.y}, {target.x, target.y});
			if (conservative)
			{
				if (scenario_crosses(shape, {origin.x, origin.y}, {target.x, target.y}))
					path += 4;
			}
			else
				path = std::max<std::int64_t>(0, path - 4);
			const auto raw = path * region.nebula->density / 1000;
			const auto factor = region.nebula->sensor_factor;
			if (raw / 1000000 > (budget - depth) / factor)
				return false;
			const auto contribution = (raw / 1000000) * factor + (raw % 1000000) * factor / 1000000;
			if (contribution > budget - depth)
				return false;
			depth += contribution;
		}
		return true;
	};
	std::int64_t lo = 0, hi = range;
	while (lo < hi)
	{
		const auto middle = lo + (hi - lo + 1) / 2;
		if (visible(middle))
			lo = middle;
		else
			hi = middle - 1;
	}
	return lo;
}
const SensorEnvelope &sensor_envelope(EntityId ship, const std::string &name, std::int64_t range) const
{
	const auto origin = entities.at(ship).get<Pose>().position;
	auto &cached = sensor_envelopes[{ship, name}];
	// Regions are immutable for a loaded World; load/restore replaces this cache.
	if (cached.range == range && cached.origin.x == origin.x && cached.origin.y == origin.y)
		return cached;
	cached = {origin, range, {}};
	if (range <= 0 || std::none_of(regions.begin(), regions.end(),
	                               [](const auto &r)
	                               {
		                               return r.nebula && r.nebula->density && r.nebula->sensor_factor;
	                               }))
		return cached;
	struct Sector
	{
		std::int64_t begin, end, low, high;
	};
	auto measure = [&](std::int64_t begin, std::int64_t end)
	{
		return Sector{begin, end, sector_reach(origin, range, begin, end, true),
		              sector_reach(origin, range, begin, end, false)};
	};
	std::vector<Sector> sectors;
	for (int i = 0; i < 16; ++i)
		sectors.push_back(measure(i * 22500, (i + 1) * 22500));
	while (sectors.size() < 1024)
	{
		auto worst = sectors.end();
		std::int64_t gap = 1000;
		for (auto it = sectors.begin(); it != sectors.end(); ++it)
			if (it->end - it->begin > 1 && it->high - it->low > gap)
			{
				worst = it;
				gap = it->high - it->low;
			}
		if (worst == sectors.end())
			break;
		const auto begin = worst->begin, end = worst->end, middle = begin + (end - begin) / 2;
		*worst = measure(begin, middle);
		sectors.push_back(measure(middle, end));
	}
	std::sort(sectors.begin(), sectors.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.begin < b.begin;
	          });
	for (const auto &s : sectors)
	{
		cached.profile.push_back({s.end, s.low});
		cached.uncertainty = std::max(cached.uncertainty, s.high - s.low);
	}
	cached.limited = cached.uncertainty > 1000;
	// Older clients receive sector minima, never sparsely sampled clear rays.
	for (int i = 0; i < 128; ++i)
	{
		const auto begin = i * 360000LL / 128, end = (i + 1) * 360000LL / 128;
		auto reach = range;
		for (const auto &s : sectors)
			if (s.begin < end && s.end > begin)
				reach = std::min(reach, s.low);
		cached.distances.push_back(reach);
	}
	return cached;
}
