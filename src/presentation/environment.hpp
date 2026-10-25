#pragma once
namespace phoenix
{
class ShaderLocations
{
	std::map<std::pair<unsigned, std::string>, int> locations_;

public:
	inline int get(Shader shader, const char *name)
	{
		auto [entry, added] = locations_.try_emplace({shader.id, name}, -1);
		if (added)
			entry->second = GetShaderLocation(shader, name);
		return entry->second;
	}
	inline void clear()
	{
		locations_.clear();
	}
};
void set_scene_fog(Shader, const Json &, Vector3 rebase, ShaderLocations &);

// Owned by one SceneRenderer; never shared between runtime and authoring views.
class NavigationEffects
{
public:
	~NavigationEffects();
	void clear();
	void background(const Catalog &, const SceneEnvironment &, Camera3D, const Json &fog = Json::object(),
	                Vector3 rebase = {});
	void draw(const Catalog &catalog, const SceneEnvironment &environment, Camera3D camera, Vector3 rebase,
	          Rectangle physical_region);

private:
	ShaderLocations locations_;
	Shader dust_shader_{}, grid_shader_{}, sky_shader_{};
	std::array<Texture2D, 3> defaults_{};
	TextureCache textures_;
};
} // namespace phoenix
