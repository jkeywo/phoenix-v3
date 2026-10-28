module;
#include "native_headers.hpp"

module phoenix.simulation;
import :command_routing;

namespace phoenix::world_detail
{
auto Runtime::flight_action(OperatingAction action) const -> std::optional<FlightCommandKind>
{
	switch (action)
	{
	case OperatingAction::Speed:
		return FlightCommandKind::Speed;
	case OperatingAction::Heading:
		return FlightCommandKind::Heading;
	case OperatingAction::EngineEnabled:
	case OperatingAction::SystemEnabled:
		return FlightCommandKind::Enabled;
	case OperatingAction::Assisted:
		return FlightCommandKind::Assisted;
	case OperatingAction::Longitudinal:
		return FlightCommandKind::Longitudinal;
	case OperatingAction::Lateral:
		return FlightCommandKind::Lateral;
	case OperatingAction::ManualSteering:
		return FlightCommandKind::Steering;
	case OperatingAction::Boost:
		return FlightCommandKind::Boost;
	case OperatingAction::Impulse:
		return FlightCommandKind::Impulse;
	case OperatingAction::MatchSpeed:
		return FlightCommandKind::MatchSpeed;
	case OperatingAction::SelectContact:
		return FlightCommandKind::SelectContact;
	case OperatingAction::Destination:
		return FlightCommandKind::Destination;
	case OperatingAction::FollowContact:
		return FlightCommandKind::FollowContact;
	default:
		return {};
	}
}
auto Runtime::resource_action(OperatingAction action) const -> std::optional<ResourceControl>
{
	switch (action)
	{
	case OperatingAction::SystemEnabled:
		return ResourceControl::Enabled;
	case OperatingAction::GeneratorOutput:
		return ResourceControl::GeneratorOutput;
	case OperatingAction::ChargeLimit:
		return ResourceControl::ChargeLimit;
	case OperatingAction::DischargeLimit:
		return ResourceControl::DischargeLimit;
	case OperatingAction::GroupAllocation:
		return ResourceControl::GroupBudget;
	case OperatingAction::LinkIsolation:
		return ResourceControl::ConduitIsolated;
	case OperatingAction::CoolingAllocation:
		return ResourceControl::CoolingAllocation;
	case OperatingAction::ThermalOverride:
		return ResourceControl::ThermalOverride;
	case OperatingAction::Overdrive:
		return ResourceControl::Overdrive;
	default:
		return {};
	}
}

} // namespace phoenix::world_detail
