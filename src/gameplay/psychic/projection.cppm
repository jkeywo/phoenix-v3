module;
#include <cstdint>
#include <span>
#include <string>
export module phoenix.gameplay.psychic.projection;
export import phoenix.gameplay.psychic.state;
export import phoenix.content.sensing.types;
export namespace phoenix
{
// Shape physics preserves its fixed-step remainders independently of reception.
void advance_psychic_shapes(PsychicContribution &contribution, const PsychicPowerDefinition &power);
BodyView psychic_ghost_body(const GhostReception &reception, const std::string &apparent_class);
bool psychic_ghost_live(const GhostReception &reception, std::uint64_t tick);
} // namespace phoenix
