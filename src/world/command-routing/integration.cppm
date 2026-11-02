module;
#include "native_headers.hpp"

export module phoenix.simulation:command_routing;
export import :runtime;

export namespace phoenix::world_detail
{
DecisionObservation decision_observation_from(const Observation &);
}
