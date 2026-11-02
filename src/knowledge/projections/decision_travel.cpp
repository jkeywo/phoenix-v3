module;
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
module phoenix.knowledge.decision_travel;
namespace phoenix
{
namespace
{
Json encode(const DockingPortDecision &v)
{
	return Json{{"id", v.id},
	            {"controller", v.controller},
	            {"compatibility", v.compatibility},
	            {"phase", v.phase},
	            {"x_mm", v.x_mm},
	            {"y_mm", v.y_mm},
	            {"heading_mdeg", v.heading_mdeg},
	            {"progress", v.progress},
	            {"accepting", v.accepting},
	            {"supply", v.supply},
	            {"working", v.working},
	            {"emergency", v.emergency},
	            {"follower", v.follower},
	            {"assisted", v.assisted},
	            {"target", v.target}};
}
Json encode(const DockingTargetDecision &v)
{
	return Json{{"entity", v.entity},
	            {"port", v.port},
	            {"compatibility", v.compatibility},
	            {"x_mm", v.x_mm},
	            {"y_mm", v.y_mm},
	            {"heading_mdeg", v.heading_mdeg},
	            {"available", v.available},
	            {"local_ports", v.local_ports}};
}
Json encode(const LogisticsStoreDecision &v)
{
	return Json{{"id", v.id},
	            {"system", v.system},
	            {"controller", v.controller},
	            {"selected_resource", v.selected_resource},
	            {"index", v.index},
	            {"main", v.main},
	            {"held", v.held},
	            {"working", v.working},
	            {"single_resource", v.single_resource},
	            {"available", v.available},
	            {"quantities", v.quantities},
	            {"space", v.space},
	            {"targets", v.targets},
	            {"resource_choices", v.resource_choices},
	            {"selectable_resources", v.selectable_resources},
	            {"resource_labels", v.resource_labels}};
}
Json encode(const LogisticsOfferDecision &v)
{
	return Json{{"entity", v.entity},
	            {"store", v.store},
	            {"material", v.material},
	            {"index", v.index},
	            {"connected", v.connected}};
}
Json encode(const LogisticsRouteDecision &v)
{
	return Json{{"from", v.from}, {"to", v.to}, {"closed", v.closed}};
}
Json encode(const LogisticsServiceDecision &v)
{
	return Json{{"bay", v.bay}, {"native", v.native}, {"process", v.process}, {"generation", v.generation}};
}
Json encode(const LogisticsCargoDecision &v)
{
	return Json{{"id", v.id},
	            {"report_tick", v.report_tick},
	            {"carrier", v.carrier},
	            {"target", v.target},
	            {"material", v.material},
	            {"phase", v.phase},
	            {"quantity", v.quantity}};
}
Json encode(const LogisticsCraftStoreDecision &v)
{
	return Json{{"id", v.id}, {"accepts", v.accepts}};
}
Json encode(const FtlDecisionRow &v)
{
	return Json{{"system", v.system},
	            {"blocker", v.blocker},
	            {"energy", v.energy},
	            {"capacity", v.capacity},
	            {"power_draw", v.power_draw},
	            {"heat", v.heat},
	            {"visitors", v.visitors},
	            {"enabled", v.enabled},
	            {"ready", v.ready},
	            {"work_completed", v.work_completed},
	            {"left_behind", v.left_behind}};
}
Json encode(const LogisticsCraftDecision &v);
template <class T> Json rows(const std::vector<T> &v)
{
	Json out = Json::array();
	for (const auto &row : v)
		out.push_back(encode(row));
	return out;
}
Json encode(const LogisticsCraftDecision &v)
{
	return {{"id", v.id},
	        {"authorised", v.authorised},
	        {"controllable", v.controllable},
	        {"available", v.available},
	        {"stores", rows(v.stores)}};
}
Json transit(const LogisticsTransitDecision &v, bool batch)
{
	Json out = {{"id", v.id},
	            {"source", v.source},
	            {"destination", v.destination},
	            {"from", v.from},
	            {"to", v.to},
	            {"material", v.material},
	            {"cargo", v.cargo},
	            {"service", encode(v.service)}};
	if (batch)
	{
		out["request"] = v.request;
		out["location"] = v.location;
		out["compartment"] = v.compartment;
		out["quantity"] = v.quantity;
		out["edge_to"] = v.edge_to;
		out["travel"] = v.travel;
		out["returning"] = v.returning;
		out["crossing"] = v.crossing;
	}
	else
	{
		out["remaining"] = v.remaining;
		out["automatic"] = v.automatic;
	}
	return out;
}
} // namespace
Json docking_decision_json(const DockingDecisionSnapshot &v)
{
	Json out = {{"revision", v.revision},
	            {"fitted", v.fitted},
	            {"complete", v.complete},
	            {"ports", rows(v.ports)},
	            {"targets", rows(v.targets)}};
	if (v.guidance)
		out["guidance"] = {{"x", v.guidance->x}, {"y", v.guidance->y}, {"navigate", v.guidance->navigate}};
	return out;
}
Json logistics_decision_json(const LogisticsDecisionSnapshot &v)
{
	Json out = {{"revision", v.revision},
	            {"manual", v.manual},
	            {"controller", v.controller},
	            {"route_controller", v.route_controller},
	            {"cargo_authorisation_controller", v.cargo_authorisation_controller},
	            {"stores", rows(v.stores)},
	            {"offers", rows(v.offers)},
	            {"routes", rows(v.routes)},
	            {"cargo", rows(v.cargo)},
	            {"cargo_craft", rows(v.cargo_craft)},
	            {"requests", Json::array()},
	            {"batches", Json::array()}};
	for (const auto &r : v.requests)
		out["requests"].push_back(transit(r, false));
	for (const auto &r : v.batches)
		out["batches"].push_back(transit(r, true));
	return out;
}
Json ftl_decision_json(const std::vector<FtlDecisionRow> &v)
{
	return rows(v);
}
} // namespace phoenix
