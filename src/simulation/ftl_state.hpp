#pragma once
namespace phoenix
{
struct FtlReserve
{
	FtlDefinition definition;
	std::int64_t discharge_remainder = 0;
};
// Frozen crew evidence at the departure boundary, never live scenario truth.
struct MissionDeparture
{
	Json report;
};
} // namespace phoenix
