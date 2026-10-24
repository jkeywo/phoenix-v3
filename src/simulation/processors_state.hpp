#pragma once
namespace phoenix
{
struct ShipProcessors
{
	std::map<std::string, ResourceProcessor> values;
	// Tick-local operating quotes; never replayed during continuation.
	std::map<std::string, ResourceAmount> energy_rates;
	std::map<std::string, ResourceAmount> work_quotes;
};
} // namespace phoenix
