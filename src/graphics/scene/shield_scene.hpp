#pragma once
namespace phoenix
{
// Shared by both scene backends. GPU state is private to one SceneRenderer.
class ShieldScene
{
public:
	ShieldScene();
	~ShieldScene();
	void clear();
	void update(std::span<const SceneBody>, EntityId hidden, const std::map<EntityId, float> &radii);
	bool active(EntityId) const;
	void draw(std::span<const SceneBody>, Camera3D, Rectangle, Vector2, unsigned framebuffer,
	          const std::map<EntityId, float> &radii);
	Json statistics() const;

private:
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace phoenix
