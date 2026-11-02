module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <string>
#include <vector>
module phoenix.knowledge.decision_travel;
namespace phoenix
{
namespace
{
template <class T> T decode(const Json &);
template <class T> std::vector<T> rows(const Json &in)
{
	std::vector<T> out;
	for (const auto &row : in)
		out.push_back(decode<T>(row));
	return out;
}
template <> DockingGuidance decode<DockingGuidance>(const Json &j)
{
	DockingGuidance v;
	v.x = j.value("x", v.x);
	v.y = j.value("y", v.y);
	v.navigate = j.value("navigate", v.navigate);
	return v;
}
template <> DockingPortDecision decode<DockingPortDecision>(const Json &j)
{
	DockingPortDecision v;
	v.id = j.value("id", v.id);
	v.controller = j.value("controller", v.controller);
	v.compatibility = j.value("compatibility", v.compatibility);
	v.phase = j.value("phase", v.phase);
	v.x_mm = j.value("x_mm", v.x_mm);
	v.y_mm = j.value("y_mm", v.y_mm);
	v.heading_mdeg = j.value("heading_mdeg", v.heading_mdeg);
	v.progress = j.value("progress", v.progress);
	v.accepting = j.value("accepting", v.accepting);
	v.supply = j.value("supply", v.supply);
	v.working = j.value("working", v.working);
	v.emergency = j.value("emergency", v.emergency);
	v.follower = j.value("follower", v.follower);
	v.assisted = j.value("assisted", v.assisted);
	v.target = j.value("target", v.target);
	return v;
}
template <> DockingTargetDecision decode<DockingTargetDecision>(const Json &j)
{
	DockingTargetDecision v;
	v.entity = j.value("entity", v.entity);
	v.port = j.value("port", v.port);
	v.compatibility = j.value("compatibility", v.compatibility);
	v.x_mm = j.value("x_mm", v.x_mm);
	v.y_mm = j.value("y_mm", v.y_mm);
	v.heading_mdeg = j.value("heading_mdeg", v.heading_mdeg);
	v.available = j.value("available", v.available);
	v.local_ports = j.value("local_ports", v.local_ports);
	return v;
}
template <> LogisticsStoreDecision decode<LogisticsStoreDecision>(const Json &j)
{
	LogisticsStoreDecision v;
	v.id = j.value("id", v.id);
	v.system = j.value("system", v.system);
	v.controller = j.value("controller", v.controller);
	v.selected_resource = j.value("selected_resource", v.selected_resource);
	v.index = j.value("index", v.index);
	v.main = j.value("main", v.main);
	v.held = j.value("held", v.held);
	v.working = j.value("working", v.working);
	v.single_resource = j.value("single_resource", v.single_resource);
	v.available = j.value("available", v.available);
	v.quantities = j.value("quantities", v.quantities);
	v.space = j.value("space", v.space);
	v.targets = j.value("targets", v.targets);
	v.resource_choices = j.value("resource_choices", v.resource_choices);
	v.selectable_resources = j.value("selectable_resources", v.selectable_resources);
	v.resource_labels = j.value("resource_labels", v.resource_labels);
	return v;
}
template <> LogisticsOfferDecision decode<LogisticsOfferDecision>(const Json &j)
{
	LogisticsOfferDecision v;
	v.entity = j.value("entity", v.entity);
	v.store = j.value("store", v.store);
	v.material = j.value("material", v.material);
	v.index = j.value("index", v.index);
	v.connected = j.value("connected", v.connected);
	return v;
}
template <> LogisticsRouteDecision decode<LogisticsRouteDecision>(const Json &j)
{
	LogisticsRouteDecision v;
	v.from = j.value("from", v.from);
	v.to = j.value("to", v.to);
	v.closed = j.value("closed", v.closed);
	return v;
}
template <> LogisticsServiceDecision decode<LogisticsServiceDecision>(const Json &j)
{
	LogisticsServiceDecision v;
	v.bay = j.value("bay", v.bay);
	v.native = j.value("native", v.native);
	v.process = j.value("process", v.process);
	v.generation = j.value("generation", v.generation);
	return v;
}
template <> LogisticsCargoDecision decode<LogisticsCargoDecision>(const Json &j)
{
	LogisticsCargoDecision v;
	v.id = j.value("id", v.id);
	v.report_tick = j.value("report_tick", v.report_tick);
	v.carrier = j.value("carrier", v.carrier);
	v.target = j.value("target", v.target);
	v.material = j.value("material", v.material);
	v.phase = j.value("phase", v.phase);
	v.quantity = j.value("quantity", v.quantity);
	return v;
}
template <> LogisticsCraftStoreDecision decode<LogisticsCraftStoreDecision>(const Json &j)
{
	LogisticsCraftStoreDecision v;
	v.id = j.value("id", v.id);
	v.accepts = j.value("accepts", v.accepts);
	return v;
}
template <> FtlDecisionRow decode<FtlDecisionRow>(const Json &j)
{
	FtlDecisionRow v;
	v.system = j.value("system", v.system);
	v.blocker = j.value("blocker", v.blocker);
	v.energy = j.value("energy", v.energy);
	v.capacity = j.value("capacity", v.capacity);
	v.power_draw = j.value("power_draw", v.power_draw);
	v.heat = j.value("heat", v.heat);
	v.visitors = j.value("visitors", v.visitors);
	v.enabled = j.value("enabled", v.enabled);
	v.ready = j.value("ready", v.ready);
	v.work_completed = j.value("work_completed", v.work_completed);
	v.left_behind = j.value("left_behind", v.left_behind);
	return v;
}
template <> LogisticsTransitDecision decode<LogisticsTransitDecision>(const Json &j)
{
	LogisticsTransitDecision v;
	v.id = j.value("id", v.id);
	v.request = j.value("request", v.request);
	v.cargo = j.value("cargo", v.cargo);
	v.source = j.value("source", v.source);
	v.destination = j.value("destination", v.destination);
	v.location = j.value("location", v.location);
	v.from = j.value("from", v.from);
	v.to = j.value("to", v.to);
	v.compartment = j.value("compartment", v.compartment);
	v.material = j.value("material", v.material);
	v.remaining = j.value("remaining", v.remaining);
	v.quantity = j.value("quantity", v.quantity);
	v.edge_to = j.value("edge_to", v.edge_to);
	v.travel = j.value("travel", v.travel);
	v.crossing = j.value("crossing", v.crossing);
	v.automatic = j.value("automatic", v.automatic);
	v.returning = j.value("returning", v.returning);
	if (j.contains("service"))
		v.service = decode<LogisticsServiceDecision>(j.at("service"));
	return v;
}
template <> LogisticsCraftDecision decode<LogisticsCraftDecision>(const Json &j)
{
	LogisticsCraftDecision v;
	v.id = j.value("id", v.id);
	v.authorised = j.value("authorised", v.authorised);
	v.controllable = j.value("controllable", v.controllable);
	v.available = j.value("available", v.available);
	v.stores = rows<LogisticsCraftStoreDecision>(j.value("stores", Json::array()));
	return v;
}
} // namespace
DockingDecisionSnapshot docking_decision_from_json(const Json &j)
{
	DockingDecisionSnapshot v;
	v.observed = true;
	v.revision = j.value("revision", v.revision);
	v.fitted = j.value("fitted", v.fitted);
	v.complete = j.value("complete", false);
	v.ports = rows<DockingPortDecision>(j.value("ports", Json::array()));
	v.targets = rows<DockingTargetDecision>(j.value("targets", Json::array()));
	if (j.contains("guidance"))
		v.guidance = decode<DockingGuidance>(j.at("guidance"));
	return v;
}
LogisticsDecisionSnapshot logistics_decision_from_json(const Json &j)
{
	LogisticsDecisionSnapshot v;
	v.observed = true;
	v.revision = j.value("revision", v.revision);
	v.manual = j.value("manual", v.manual);
	v.controller = j.value("controller", v.controller);
	v.route_controller = j.value("route_controller", v.route_controller);
	v.cargo_authorisation_controller = j.value("cargo_authorisation_controller", v.cargo_authorisation_controller);
	v.stores = rows<LogisticsStoreDecision>(j.value("stores", Json::array()));
	v.offers = rows<LogisticsOfferDecision>(j.value("offers", Json::array()));
	v.routes = rows<LogisticsRouteDecision>(j.value("routes", Json::array()));
	v.requests = rows<LogisticsTransitDecision>(j.value("requests", Json::array()));
	v.batches = rows<LogisticsTransitDecision>(j.value("batches", Json::array()));
	v.cargo = rows<LogisticsCargoDecision>(j.value("cargo", Json::array()));
	v.cargo_craft = rows<LogisticsCraftDecision>(j.value("cargo_craft", Json::array()));
	return v;
}
std::vector<FtlDecisionRow> ftl_decision_from_json(const Json &j)
{
	return rows<FtlDecisionRow>(j);
}
} // namespace phoenix
