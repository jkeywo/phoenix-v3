module;
#include "native_headers.hpp"

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::material_gateway(EntityId source, EntityId destination, bool finishing) const
    -> std::optional<MaterialGateway>
{
	auto docking = [&](EntityId child, EntityId parent, bool reverse) -> std::optional<MaterialGateway>
	{
		const auto *c = entities.at(child).try_get<DockConnection>();
		if (!c || c->parent != parent || (c->phase != "docked" && !(finishing && c->phase == "separating")) ||
		    !dock_port_working(child, c->port) || !dock_port_working(parent, c->remote_port))
			return {};
		if (!entities.at(child).get<DockingState>().ports[c->port].supply ||
		    !entities.at(parent).get<DockingState>().ports[c->remote_port].supply)
			return {};
		const auto &a = definition(child).docking[c->port], &b = definition(parent).docking[c->remote_port];
		const auto ca = definition(child).combat->compartment_ids.at(a.compartment),
		           cb = definition(parent).combat->compartment_ids.at(b.compartment);
		return MaterialGateway{reverse ? cb : ca, reverse ? ca : cb, std::min(a.throughput, b.throughput)};
	};
	if (auto g = docking(source, destination, false))
		return g;
	if (auto g = docking(destination, source, true))
		return g;
	auto bay = [&](EntityId craft, EntityId home, bool reverse) -> std::optional<MaterialGateway>
	{
		const auto *s = entities.at(craft).try_get<CraftState>();
		if (!s || s->home != home || s->phase != CraftPhase::Docked || !bay_working(home, s->bay))
			return {};
		const auto &b = definition(home).craft_bays.at(s->bay);
		const auto from = definition(home).combat->compartment_ids.at(b.compartment);
		const auto &p = definition(craft).transport.compartment;
		const auto to = p.empty() ? 0 : definition(craft).combat->compartment_ids.at(p);
		return MaterialGateway{reverse ? to : from, reverse ? from : to,
		                       std::min(definition(home).logistics.throughput, definition(craft).logistics.throughput)};
	};
	if (auto g = bay(destination, source, false))
		return g;
	return bay(source, destination, true);
}

auto Runtime::dock_chain_contains(EntityId id, EntityId needle) const -> bool
{
	std::set<EntityId> seen;
	while (true)
	{
		if (id == needle)
			return true;
		if (!seen.insert(id).second)
			throw std::runtime_error("Cyclic docking request");
		const auto *c = entities.at(id).try_get<DockConnection>();
		if (!c)
			return false;
		id = c->parent;
	}
}

auto Runtime::dock_report_available(const ConsoleAddress &a, EntityId target) const -> bool
{
	for (const auto &contact : capture_sensor_contacts(a))
		if (contact.id == target)
			return true;
	const auto x = conversations.endpoints.find(a.ship), y = conversations.endpoints.find(target);
	return x != conversations.endpoints.end() && y != conversations.endpoints.end() &&
	       comms_endpoints_linked(x->second, y->second);
}
} // namespace phoenix::world_detail
