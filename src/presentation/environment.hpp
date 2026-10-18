#pragma once
namespace phoenix
{
// Owned by one SceneRenderer; never shared between runtime and authoring views.
class NavigationEffects
{
public:
	~NavigationEffects();
	void clear();
	void draw(const Catalog &catalog, const SceneEnvironment &environment, Camera3D camera, Vector3 rebase,
	          Rectangle physical_region);

private:
	Shader dust_shader_{}, grid_shader_{};
	std::array<Texture2D, 3> defaults_{};
	TextureCache textures_;
};
} // namespace phoenix
