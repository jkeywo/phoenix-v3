#pragma once
#include "authority.hpp"
namespace phoenix
{
struct PreparedFleet
{
	Json plan;
	std::string self;
	std::shared_ptr<HostAccess> world;
	Bytes bundle;
};
// A private setup bundle contains the exact packs and the fixed member credential.
// Only its inner gameplay archives may be advertised to player/GM clients.
Bytes export_fleet_setup(const HostAccess &source, const GameSetup &selection, const std::string &room);
PreparedFleet read_fleet_setup(const Bytes &archive, const std::string &member = {});
} // namespace phoenix
