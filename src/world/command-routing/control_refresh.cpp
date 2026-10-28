module;
#include "native_headers.hpp"

module phoenix.simulation;
import :command_routing;

namespace phoenix::world_detail
{
auto Runtime::refresh_control(EntityId ship, bool transitions) -> void
{
	refresh_controllers(ship, !transitions);
}
} // namespace phoenix::world_detail
