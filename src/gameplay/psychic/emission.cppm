module;
#include <flecs.h>
#include <cstdint>
export module phoenix.gameplay.psychic.emission;
export import phoenix.contracts;
export import phoenix.conversations;
export namespace phoenix
{
std::int64_t native_psychic_emission(flecs::entity entity, EntityId ship, bool unavailable, bool recent_weapon,
                                     const ConversationRuntime &conversations, std::uint64_t tick);
}
