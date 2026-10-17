#include <phoenix/world.hpp>
#include <flecs.h>
namespace phoenix {
struct Clock { unsigned long long tick = 0; };
struct World::Impl { flecs::world ecs; Impl() { ecs.set<Clock>({0}); } };
World::World() : impl_(std::make_unique<Impl>()) {}
World::~World() = default;
void World::tick() { ++impl_->ecs.get_mut<Clock>().tick; }
unsigned long long World::ticks() const { return impl_->ecs.get<Clock>().tick; }
}
