#pragma once
namespace phoenix
{
struct ShipProcessors
{
	std::map<std::string, ResourceProcessor> values;
	// Tick-local operating quotes; never replayed during continuation.
	std::map<std::string, ResourceAmount> energy_rates;
};
} // namespace phoenix
