module;
#include <flecs.h>
#include <algorithm>
#include <cstdint>
#include <string>

export module phoenix.gameplay.machinery;
export import phoenix.effects.processes;
export import phoenix.content.operations.types;

export namespace phoenix
{
struct NativeMachineryState
{
	bool unavailable = false, dormant = false, docked = false;
};
std::string native_machinery_blocker(flecs::entity ship, const CustomPropertyAddress &, const ProcessState &,
                                     const ProcessDefinition &, const PersonnelDefinition *, NativeMachineryState);
} // namespace phoenix
