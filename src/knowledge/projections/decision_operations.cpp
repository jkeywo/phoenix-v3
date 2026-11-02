module;
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <map>
module phoenix.knowledge.decision_operations;
namespace phoenix
{
void to_json(Json &j, const AwaySlotDecision &v)
{
	j = Json::object();
	j["id"] = v.id;
	j["assigned"] = v.assigned;
	j["required"] = v.required;
	j["owned"] = v.owned;
	j["choices"] = v.choices;
	if (v.craft)
	{
		j["role"] = v.role;
		j["lost"] = v.lost;
	}
}
void from_json(const Json &j, AwaySlotDecision &v)
{
	v = {};
	if (j.contains("id"))
		j.at("id").get_to(v.id);
	if (j.contains("assigned"))
		j.at("assigned").get_to(v.assigned);
	if (j.contains("required"))
		j.at("required").get_to(v.required);
	if (j.contains("owned"))
		j.at("owned").get_to(v.owned);
	if (j.contains("choices"))
		j.at("choices").get_to(v.choices);
	v.craft = j.contains("role");
	v.role = j.value("role", std::string{});
	v.lost = j.value("lost", false);
}
void to_json(Json &j, const AwaySupplyDecision &v)
{
	j = Json::object();
	j["material"] = v.material;
	j["payment"] = v.payment;
	j["phase"] = v.phase;
	j["give"] = v.give;
	j["pay"] = v.pay;
	j["quantity"] = v.quantity;
	j["payment_quantity"] = v.payment_quantity;
	j["delivered"] = v.delivered;
	j["manual"] = v.manual;
	j["quantities"] = v.quantities;
}
void from_json(const Json &j, AwaySupplyDecision &v)
{
	v = {};
	if (j.contains("material"))
		j.at("material").get_to(v.material);
	if (j.contains("payment"))
		j.at("payment").get_to(v.payment);
	if (j.contains("phase"))
		j.at("phase").get_to(v.phase);
	if (j.contains("give"))
		j.at("give").get_to(v.give);
	if (j.contains("pay"))
		j.at("pay").get_to(v.pay);
	if (j.contains("quantity"))
		j.at("quantity").get_to(v.quantity);
	if (j.contains("payment_quantity"))
		j.at("payment_quantity").get_to(v.payment_quantity);
	if (j.contains("delivered"))
		j.at("delivered").get_to(v.delivered);
	if (j.contains("manual"))
		j.at("manual").get_to(v.manual);
	if (j.contains("quantities"))
		j.at("quantities").get_to(v.quantities);
}
void to_json(Json &j, const AwayBoardingDecision &v)
{
	j = Json::object();
	j["chance_permille"] = v.chance_permille;
	j["defenders"] = v.defenders;
	j["encounter"] = v.encounter;
	j["factors"] = v.factors;
}
void from_json(const Json &j, AwayBoardingDecision &v)
{
	v = {};
	if (j.contains("chance_permille"))
		j.at("chance_permille").get_to(v.chance_permille);
	if (j.contains("defenders"))
		j.at("defenders").get_to(v.defenders);
	if (j.contains("encounter"))
		j.at("encounter").get_to(v.encounter);
	if (j.contains("factors"))
		j.at("factors").get_to(v.factors);
}
void to_json(Json &j, const AwayPeerDecision &v)
{
	j = Json::object();
	j["console"] = v.console;
	j["controller"] = v.controller;
	j["id"] = v.id;
}
void from_json(const Json &j, AwayPeerDecision &v)
{
	v = {};
	if (j.contains("console"))
		j.at("console").get_to(v.console);
	if (j.contains("controller"))
		j.at("controller").get_to(v.controller);
	if (j.contains("id"))
		j.at("id").get_to(v.id);
}
void to_json(Json &j, const AwayDepartmentDecision &v)
{
	j = Json::object();
	j["assigned"] = v.assigned;
	j["ready"] = v.ready;
	j["contributing"] = v.contributing;
	j["transport_released"] = v.transport_released;
	j["recall_requested"] = v.recall_requested;
	j["launch_ready"] = v.launch_ready;
	j["observed_tick"] = v.observed_tick;
	j["revision"] = v.revision;
	j["peers"] = v.peers;
	j["transport_systems"] = v.transport_systems;
}
void from_json(const Json &j, AwayDepartmentDecision &v)
{
	v = {};
	if (j.contains("assigned"))
		j.at("assigned").get_to(v.assigned);
	if (j.contains("ready"))
		j.at("ready").get_to(v.ready);
	if (j.contains("contributing"))
		j.at("contributing").get_to(v.contributing);
	if (j.contains("transport_released"))
		j.at("transport_released").get_to(v.transport_released);
	if (j.contains("recall_requested"))
		j.at("recall_requested").get_to(v.recall_requested);
	if (j.contains("launch_ready"))
		j.at("launch_ready").get_to(v.launch_ready);
	if (j.contains("observed_tick"))
		j.at("observed_tick").get_to(v.observed_tick);
	if (j.contains("revision"))
		j.at("revision").get_to(v.revision);
	if (j.contains("peers"))
		j.at("peers").get_to(v.peers);
	if (j.contains("transport_systems"))
		j.at("transport_systems").get_to(v.transport_systems);
}
void to_json(Json &j, const AwayCarePatientDecision &v)
{
	j = Json::object();
	j["id"] = v.id;
	j["condition"] = v.condition;
	j["stabilised"] = v.stabilised;
}
void from_json(const Json &j, AwayCarePatientDecision &v)
{
	v = {};
	if (j.contains("id"))
		j.at("id").get_to(v.id);
	if (j.contains("condition"))
		j.at("condition").get_to(v.condition);
	if (j.contains("stabilised"))
		j.at("stabilised").get_to(v.stabilised);
}
void to_json(Json &j, const AwayCareDecision &v)
{
	j = Json::object();
	j["team"] = v.team;
	j["controller"] = v.controller;
	j["location"] = v.location;
	j["destination"] = v.destination;
	j["travel"] = v.travel;
	j["local_complete"] = v.local_complete;
	j["observed_tick"] = v.observed_tick;
	j["compartments"] = v.compartments;
	j["patients"] = v.patients;
}
void from_json(const Json &j, AwayCareDecision &v)
{
	v = {};
	if (j.contains("team"))
		j.at("team").get_to(v.team);
	if (j.contains("controller"))
		j.at("controller").get_to(v.controller);
	if (j.contains("location"))
		j.at("location").get_to(v.location);
	if (j.contains("destination"))
		j.at("destination").get_to(v.destination);
	if (j.contains("travel"))
		j.at("travel").get_to(v.travel);
	if (j.contains("local_complete"))
		j.at("local_complete").get_to(v.local_complete);
	if (j.contains("observed_tick"))
		j.at("observed_tick").get_to(v.observed_tick);
	if (j.contains("compartments"))
		j.at("compartments").get_to(v.compartments);
	if (j.contains("patients"))
		j.at("patients").get_to(v.patients);
}
void to_json(Json &j, const AwayMissionDecision &v)
{
	j = Json::object();
	j["id"] = v.id;
	j["title"] = v.title;
	j["phase"] = v.phase;
	j["result"] = v.result;
	j["transport"] = v.transport;
	j["owner"] = v.owner;
	j["dispatch_system"] = v.dispatch_system;
	j["blocker"] = v.blocker;
	j["ship_transport_reason"] = v.ship_transport_reason;
	j["ship_support"] = v.ship_support;
	j["launch_blocker"] = v.launch_blocker;
	j["revision"] = v.revision;
	j["target"] = v.target;
	j["dispatch"] = v.dispatch;
	j["ai_dispatch"] = v.ai_dispatch;
	j["automatic_dispatch"] = v.automatic_dispatch;
	j["can_contribute"] = v.can_contribute;
	j["contributing"] = v.contributing;
	j["transport_owned"] = v.transport_owned;
	j["launch_ready"] = v.launch_ready;
	j["work_complete"] = v.work_complete;
	j["slots"] = v.slots;
	j["craft_slots"] = v.craft_slots;
	j["transports"] = v.transports;
	j["accompanying"] = v.accompanying;
	j["docking_ports"] = v.docking_ports;
	j["labels"] = v.labels;
	j["portraits"] = v.portraits;
	j["crew_names"] = v.crew_names;
	if (v.supply)
		j["supply"] = *v.supply;
	if (v.boarding_check)
		j["boarding_check"] = *v.boarding_check;
	if (v.department)
		j["department"] = *v.department;
	if (v.care_reported)
		j["care"] = v.care;
}
void from_json(const Json &j, AwayMissionDecision &v)
{
	v = {};
	if (j.contains("id"))
		j.at("id").get_to(v.id);
	if (j.contains("title"))
		j.at("title").get_to(v.title);
	if (j.contains("phase"))
		j.at("phase").get_to(v.phase);
	if (j.contains("result"))
		j.at("result").get_to(v.result);
	if (j.contains("transport"))
		j.at("transport").get_to(v.transport);
	if (j.contains("owner"))
		j.at("owner").get_to(v.owner);
	if (j.contains("dispatch_system"))
		j.at("dispatch_system").get_to(v.dispatch_system);
	if (j.contains("blocker"))
		j.at("blocker").get_to(v.blocker);
	if (j.contains("ship_transport_reason"))
		j.at("ship_transport_reason").get_to(v.ship_transport_reason);
	if (j.contains("ship_support"))
		j.at("ship_support").get_to(v.ship_support);
	if (j.contains("launch_blocker"))
		j.at("launch_blocker").get_to(v.launch_blocker);
	if (j.contains("revision"))
		j.at("revision").get_to(v.revision);
	if (j.contains("target"))
		j.at("target").get_to(v.target);
	if (j.contains("dispatch"))
		j.at("dispatch").get_to(v.dispatch);
	if (j.contains("ai_dispatch"))
		j.at("ai_dispatch").get_to(v.ai_dispatch);
	if (j.contains("automatic_dispatch"))
		j.at("automatic_dispatch").get_to(v.automatic_dispatch);
	if (j.contains("can_contribute"))
		j.at("can_contribute").get_to(v.can_contribute);
	if (j.contains("contributing"))
		j.at("contributing").get_to(v.contributing);
	if (j.contains("transport_owned"))
		j.at("transport_owned").get_to(v.transport_owned);
	if (j.contains("launch_ready"))
		j.at("launch_ready").get_to(v.launch_ready);
	if (j.contains("work_complete"))
		j.at("work_complete").get_to(v.work_complete);
	if (j.contains("slots"))
		j.at("slots").get_to(v.slots);
	if (j.contains("craft_slots"))
		j.at("craft_slots").get_to(v.craft_slots);
	if (j.contains("transports"))
		j.at("transports").get_to(v.transports);
	if (j.contains("accompanying"))
		j.at("accompanying").get_to(v.accompanying);
	if (j.contains("docking_ports"))
		j.at("docking_ports").get_to(v.docking_ports);
	if (j.contains("labels"))
		j.at("labels").get_to(v.labels);
	if (j.contains("portraits"))
		j.at("portraits").get_to(v.portraits);
	if (j.contains("crew_names"))
		j.at("crew_names").get_to(v.crew_names);
	if (j.contains("supply"))
		v.supply = j.at("supply").get<AwaySupplyDecision>();
	if (j.contains("boarding_check"))
		v.boarding_check = j.at("boarding_check").get<AwayBoardingDecision>();
	if (j.contains("department"))
		v.department = j.at("department").get<AwayDepartmentDecision>();
	v.care_reported = j.contains("care");
	if (v.care_reported)
		j.at("care").get_to(v.care);
}
void to_json(Json &j, const CraftDecisionRow &v)
{
	j = Json::object();
	j["id"] = v.id;
	j["target"] = v.target;
	j["bay"] = v.bay;
	j["role"] = v.role;
	j["phase"] = v.phase;
	j["order"] = v.order;
	j["pending_order"] = v.pending_order;
	j["mission"] = v.mission;
	j["blocker"] = v.blocker;
	j["revision"] = v.revision;
	j["report_tick"] = v.report_tick;
	j["linked"] = v.linked;
	j["pending"] = v.pending;
	j["reserved"] = v.reserved;
	j["ftl_preparing"] = v.ftl_preparing;
	j["available"] = v.available;
	j["recovery_available"] = v.recovery_available;
	j["recall_active"] = v.recall_active;
	j["armed"] = v.armed;
	j["manual"] = v.manual;
	j["return_reserve_permille"] = v.return_reserve_permille;
	j["transport_capacity"] = v.transport_capacity;
	j["energy"] = v.energy;
	j["capacity"] = v.capacity;
	j["ammunition"] = v.ammunition;
	j["condition"] = v.condition;
	j["maximum_condition"] = v.maximum_condition;
}
void from_json(const Json &j, CraftDecisionRow &v)
{
	v = {};
	if (j.contains("id"))
		j.at("id").get_to(v.id);
	if (j.contains("target"))
		j.at("target").get_to(v.target);
	if (j.contains("bay"))
		j.at("bay").get_to(v.bay);
	if (j.contains("role"))
		j.at("role").get_to(v.role);
	if (j.contains("phase"))
		j.at("phase").get_to(v.phase);
	if (j.contains("order"))
		j.at("order").get_to(v.order);
	if (j.contains("pending_order"))
		j.at("pending_order").get_to(v.pending_order);
	if (j.contains("mission"))
		j.at("mission").get_to(v.mission);
	if (j.contains("blocker"))
		j.at("blocker").get_to(v.blocker);
	if (j.contains("revision"))
		j.at("revision").get_to(v.revision);
	if (j.contains("report_tick"))
		j.at("report_tick").get_to(v.report_tick);
	if (j.contains("linked"))
		j.at("linked").get_to(v.linked);
	if (j.contains("pending"))
		j.at("pending").get_to(v.pending);
	if (j.contains("reserved"))
		j.at("reserved").get_to(v.reserved);
	if (j.contains("ftl_preparing"))
		j.at("ftl_preparing").get_to(v.ftl_preparing);
	if (j.contains("available"))
		j.at("available").get_to(v.available);
	if (j.contains("recovery_available"))
		j.at("recovery_available").get_to(v.recovery_available);
	if (j.contains("recall_active"))
		j.at("recall_active").get_to(v.recall_active);
	if (j.contains("armed"))
		j.at("armed").get_to(v.armed);
	if (j.contains("manual"))
		j.at("manual").get_to(v.manual);
	if (j.contains("return_reserve_permille"))
		j.at("return_reserve_permille").get_to(v.return_reserve_permille);
	if (j.contains("transport_capacity"))
		j.at("transport_capacity").get_to(v.transport_capacity);
	if (j.contains("energy"))
		j.at("energy").get_to(v.energy);
	if (j.contains("capacity"))
		j.at("capacity").get_to(v.capacity);
	if (j.contains("ammunition"))
		j.at("ammunition").get_to(v.ammunition);
	if (j.contains("condition"))
		j.at("condition").get_to(v.condition);
	if (j.contains("maximum_condition"))
		j.at("maximum_condition").get_to(v.maximum_condition);
	if (!v.id.empty())
		v.entity = std::stoull(v.id);
}
void to_json(Json &j, const CraftGuidanceDecision &v)
{
	j = Json::object();
	j["x"] = v.x;
	j["y"] = v.y;
	j["altitude"] = v.altitude;
	j["speed"] = v.speed;
	j["heading"] = v.heading;
	j["target"] = v.target;
	j["navigate"] = v.navigate;
}
void from_json(const Json &j, CraftGuidanceDecision &v)
{
	v = {};
	if (j.contains("x"))
		j.at("x").get_to(v.x);
	if (j.contains("y"))
		j.at("y").get_to(v.y);
	if (j.contains("altitude"))
		j.at("altitude").get_to(v.altitude);
	if (j.contains("speed"))
		j.at("speed").get_to(v.speed);
	if (j.contains("heading"))
		j.at("heading").get_to(v.heading);
	if (j.contains("target"))
		j.at("target").get_to(v.target);
	if (j.contains("navigate"))
		j.at("navigate").get_to(v.navigate);
}
void to_json(Json &j, const CraftSelfDecision &v)
{
	j = Json::object();
	j["order"] = v.order;
	j["target"] = v.target;
	j["reserve_low"] = v.reserve_low;
	j["armed"] = v.armed;
	j["anchor_x"] = v.anchor_x;
	j["anchor_y"] = v.anchor_y;
	j["engagement_range"] = v.engagement_range;
	j["pursuit_altitude_offset"] = v.pursuit_altitude_offset;
	if (v.guidance)
		j["guidance"] = *v.guidance;
}
void from_json(const Json &j, CraftSelfDecision &v)
{
	v = {};
	if (j.contains("order"))
		j.at("order").get_to(v.order);
	if (j.contains("target"))
		j.at("target").get_to(v.target);
	if (j.contains("reserve_low"))
		j.at("reserve_low").get_to(v.reserve_low);
	if (j.contains("armed"))
		j.at("armed").get_to(v.armed);
	if (j.contains("anchor_x"))
		j.at("anchor_x").get_to(v.anchor_x);
	if (j.contains("anchor_y"))
		j.at("anchor_y").get_to(v.anchor_y);
	if (j.contains("engagement_range"))
		j.at("engagement_range").get_to(v.engagement_range);
	if (j.contains("pursuit_altitude_offset"))
		j.at("pursuit_altitude_offset").get_to(v.pursuit_altitude_offset);
	if (j.contains("guidance"))
		v.guidance = j.at("guidance").get<CraftGuidanceDecision>();
}
} // namespace phoenix
