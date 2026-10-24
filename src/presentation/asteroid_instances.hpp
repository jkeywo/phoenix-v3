// Persistent GPU records. Camera/time changes never dirty these buffers.
struct InstanceBatch
{
	MaterialData *material = nullptr;
	Vector3 origin{};
	std::vector<std::array<float, 12>> records;
	std::map<EntityId, std::size_t> slots;
	std::vector<EntityId> identities;
	std::set<EntityId> seen;
	unsigned buffer = 0;
	Texture2D metadata{};
	std::set<std::size_t> dirty;
	float extent_scale = 1;
	Vector3 minimum{}, maximum{};
	bool colour_visible = true, shadow_visible = true, bounds_dirty = false;
	std::size_t capacity = 0;
};
std::map<std::tuple<std::string, std::int64_t, std::int64_t>, InstanceBatch> instance_batches;
std::uint64_t instance_uploads = 0, instance_records_uploaded = 0, instance_allocations = 0;
float shadow_coverage = 0;
void clear_instances()
{
	for (auto &[key, b] : instance_batches)
	{
		if (b.buffer)
			rlUnloadVertexBuffer(b.buffer);
		if (b.metadata.id)
			UnloadTexture(b.metadata);
	}
	instance_batches.clear();
}
void prepare_instances(const Catalog &catalog, std::span<const SceneBody> bodies)
{
	for (auto &[key, b] : instance_batches)
		b.seen.clear();
	constexpr std::int64_t chunk = 256000;
	auto cell = [](std::int64_t x)
	{
		return x >= 0 ? x / chunk : -((-x + chunk - 1) / chunk);
	};
	for (const auto &s : bodies)
	{
		if (!s.field_instance)
			continue;
		const auto cx = cell(s.x), cy = cell(s.y);
		auto &b = instance_batches[{s.definition, cx, cy}];
		if (!b.material)
		{
			const auto &render = catalog.entities.at(s.definition).at("render");
			b.material = &load(s.definition, render.at("sprite"), catalog);
			b.origin = {cx * 256.f, 0, -cy * 256.f};
			const auto &m = b.material->sprite;
			const auto bounds = [&](const auto &frames)
			{
				for (const auto &f : frames)
				{
					const float x = std::max(std::abs(f.pivot[0]), std::abs(f.rect[2] - f.pivot[0]));
					const float y = std::max(std::abs(f.pivot[1]), std::abs(f.rect[3] - f.pivot[1]));
					b.extent_scale =
					    std::max(b.extent_scale, std::sqrt(x * x + y * y) / f.rect[3] + std::abs(f.depth_offset) +
					                                 std::abs(f.depth_scale) * .5f);
				}
			};
			if (m.entries.empty())
				bounds(m.frames);
			else
				for (const auto &entry : m.entries)
					bounds(entry.frames);
			const auto entries =
			    m.entries.empty() ? std::vector<SpriteMaterial::Entry>{{"default", m.frames}} : m.entries;
			const int rows = static_cast<int>((entries.size() * 25 + 255) / 256);
			std::vector<float> pixels(rows * 256 * 4, 0);
			auto put = [&](std::size_t index, std::array<float, 4> value)
			{
				std::copy(value.begin(), value.end(), pixels.begin() + index * 4);
			};
			for (std::size_t i = 0; i < entries.size(); ++i)
			{
				put(i * 25, {static_cast<float>(entries[i].frames.size()), 0, 0, 0});
				for (std::size_t j = 0; j < entries[i].frames.size(); ++j)
				{
					const auto &f = entries[i].frames[j];
					put(i * 25 + 1 + j * 3, {static_cast<float>(f.rect[0]), static_cast<float>(f.rect[1]),
					                         static_cast<float>(f.rect[2]), static_cast<float>(f.rect[3])});
					put(i * 25 + 2 + j * 3, {f.pivot[0], f.pivot[1], f.depth_scale, f.depth_offset});
					put(i * 25 + 3 + j * 3, {f.azimuth, f.mirror_x ? 1.f : 0.f, 0, 0});
				}
			}
			b.metadata = {rlLoadTexture(pixels.data(), 256, rows, PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 1), 256, rows,
			              1, PIXELFORMAT_UNCOMPRESSED_R32G32B32A32};
			if (!b.metadata.id)
				throw std::runtime_error("Unable to allocate sprite entry metadata");
			SetTextureFilter(b.metadata, TEXTURE_FILTER_POINT);
		}
		std::array<float, 12> record{(s.x - cx * chunk) / 1000.f,
		                             s.visual_height_mm / 1000.f,
		                             -(s.y - cy * chunk) / 1000.f,
		                             catalog.entities.at(s.definition).at("render").at("scale").get<float>(),
		                             s.rotation_mdeg * pi / 180000.f,
		                             s.spin_mdeg_s * pi / 180000.f,
		                             0,
		                             0,
		                             static_cast<float>(s.sprite_entry),
		                             s.heading / 1000.f,
		                             0,
		                             0};
		auto [slot, added] = b.slots.try_emplace(s.id, b.records.size());
		if (added)
		{
			b.records.push_back(record);
			b.identities.push_back(s.id);
			b.dirty.insert(slot->second);
		}
		else if (b.records[slot->second] != record)
		{
			b.records[slot->second] = record;
			b.dirty.insert(slot->second);
		}
		b.seen.insert(s.id);
	}
	for (auto it = instance_batches.begin(); it != instance_batches.end();)
	{
		auto &b = it->second;
		for (auto entry = b.slots.begin(); entry != b.slots.end();)
		{
			if (b.seen.contains(entry->first))
			{
				++entry;
				continue;
			}
			const auto index = entry->second, last = b.records.size() - 1;
			if (index != last)
			{
				b.records[index] = b.records.back();
				b.identities[index] = b.identities.back();
				b.slots.at(b.identities[index]) = index;
				b.dirty.insert(index);
			}
			b.records.pop_back();
			b.identities.pop_back();
			b.bounds_dirty = true;
			entry = b.slots.erase(entry);
		}
		if (b.records.empty())
		{
			if (b.buffer)
				rlUnloadVertexBuffer(b.buffer);
			UnloadTexture(b.metadata);
			it = instance_batches.erase(it);
			continue;
		}
		const auto bytes = b.records.size() * sizeof(b.records[0]);
		if (bytes > b.capacity)
		{
			if (b.buffer)
				rlUnloadVertexBuffer(b.buffer);
			b.capacity = std::max<std::size_t>(sizeof(b.records[0]) * 16, b.capacity * 2);
			while (b.capacity < bytes)
				b.capacity *= 2;
			b.buffer = rlLoadVertexBuffer(nullptr, static_cast<int>(b.capacity), true);
			if (!b.buffer)
				throw std::runtime_error("Unable to allocate asteroid instance buffer");
			++instance_allocations;
			for (std::size_t i = 0; i < b.records.size(); ++i)
				b.dirty.insert(i);
		}
		if (b.bounds_dirty || !b.dirty.empty())
		{
			b.minimum = {1e30f, 1e30f, 1e30f};
			b.maximum = {-1e30f, -1e30f, -1e30f};
			for (const auto &record : b.records)
			{
				const auto extent = record[3] * b.extent_scale;
				b.minimum = Vector3Min(b.minimum, {record[0] - extent, record[1] - extent, record[2] - extent});
				b.maximum = Vector3Max(b.maximum, {record[0] + extent, record[1] + extent, record[2] + extent});
			}
			auto dirty = b.dirty.begin();
			while (dirty != b.dirty.end() && *dirty < b.records.size())
			{
				const auto first = *dirty;
				auto end = first + 1;
				++dirty;
				while (dirty != b.dirty.end() && *dirty == end && *dirty < b.records.size())
				{
					++end;
					++dirty;
				}
				rlUpdateVertexBuffer(b.buffer, b.records.data() + first,
				                     static_cast<int>((end - first) * sizeof(b.records[0])),
				                     static_cast<int>(first * sizeof(b.records[0])));
				++instance_uploads;
				instance_records_uploaded += end - first;
			}
			b.dirty.clear();
			b.bounds_dirty = false;
		}
		++it;
	}
}
float item_extent(const Item &item) const
{
	const auto &f = *item.frame;
	const float x = std::max(std::abs(f.pivot[0]), std::abs(f.rect[2] - f.pivot[0])),
	            y = std::max(std::abs(f.pivot[1]), std::abs(f.rect[3] - f.pivot[1]));
	return item.height *
	       (std::sqrt(x * x + y * y) / f.rect[3] + std::abs(f.depth_offset) + std::abs(f.depth_scale) * .5f);
}
Matrix cull_instances(Camera3D camera, Rectangle region)
{
	const auto vp = MatrixMultiply(
	    MatrixLookAt(Vector3Subtract(camera.position, rebase), Vector3Subtract(camera.target, rebase), camera.up),
	    scene_camera_projection(camera, region));
	for (auto &[key, b] : instance_batches)
	{
		const auto origin = Vector3Subtract(b.origin, rebase);
		b.colour_visible = scene_bounds_visible(Vector3Add(origin, b.minimum), Vector3Add(origin, b.maximum), vp);
	}
	for (auto &item : items)
	{
		const float radius = item_extent(item);
		item.colour_visible = scene_bounds_visible(Vector3Subtract(item.anchor, {radius, radius, radius}),
		                                           Vector3Add(item.anchor, {radius, radius, radius}), vp);
	}
	return vp;
}

void draw_instances(bool shadow_pass)
{
	rlDrawRenderBatchActive();
	integer("instanced", 1);
	vec("basisRight", right);
	vec("basisUp", up);
	vec("basisNear", near);
	vec("cameraPosition", cameraPosition);
	number("paperLod", 1);
	rlEnableShader(shader.id);
	SetShaderValueMatrix(shader, loc("mvp"), MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection()));
	for (const auto &[key, b] : instance_batches)
	{
		auto &m = *b.material;
		if (!(shadow_pass ? b.shadow_visible : b.colour_visible))
			continue;
		if (shadow_pass && m.sprite.emissive)
			continue;
		vec("chunkOrigin", Vector3Subtract(b.origin, rebase));
		integer("hasDepth", m.sprite.depth.empty() ? 0 : 1);
		integer("hasNormal", m.sprite.normal.empty() ? 0 : 1);
		integer("emissive", m.sprite.emissive ? 1 : 0);
		Vector2 atlas{static_cast<float>(m.albedo.width), static_cast<float>(m.albedo.height)};
		SetShaderValue(shader, loc("atlasSize"), &atlas, SHADER_UNIFORM_VEC2);
		const std::array textures{m.albedo.id, m.depth.id, m.normal.id, shadow_pass ? white.id : shadow.texture.id,
		                          b.metadata.id};
		const std::array names{"texture0", "depthMap", "normalMap", "shadowMap", "frameMetadata"};
		for (int slot = 0; slot < 5; ++slot)
		{
			rlActiveTextureSlot(slot);
			rlEnableTexture(textures[slot]);
			integer(names[slot], slot);
		}
		rlEnableVertexArray(grid.vaoId);
		rlEnableVertexBuffer(b.buffer);
		for (unsigned a = 0; a < 3; ++a)
		{
			rlSetVertexAttribute(8 + a, 4, RL_FLOAT, false, 12 * sizeof(float),
			                     static_cast<int>(a * 4 * sizeof(float)));
			rlEnableVertexAttribute(8 + a);
			rlSetVertexAttributeDivisor(8 + a, 1);
		}
		rlDrawVertexArrayElementsInstanced(0, grid.triangleCount * 3, nullptr, static_cast<int>(b.records.size()));
		for (unsigned a = 0; a < 3; ++a)
		{
			rlSetVertexAttributeDivisor(8 + a, 0);
			rlDisableVertexAttribute(8 + a);
		}
		rlDisableVertexArray();
		rlDisableVertexBuffer();
	}
	for (int slot = 4; slot >= 0; --slot)
	{
		rlActiveTextureSlot(slot);
		rlDisableTexture();
	}
	rlDisableShader();
	integer("instanced", 0);
}
