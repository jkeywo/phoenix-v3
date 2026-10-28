module;
#include <cstddef>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

module phoenix.gameplay.logistics.state;

namespace phoenix
{
void to_json(Json &json, const MaterialServiceAttribution &value)
{
	json["bay"] = value.bay;
	json["native"] = value.native;
	json["process"] = value.process;
	json["generation"] = value.generation;
}
void from_json(const Json &json, MaterialServiceAttribution &value)
{
	json.at("bay").get_to(value.bay);
	json.at("native").get_to(value.native);
	json.at("process").get_to(value.process);
	json.at("generation").get_to(value.generation);
}
void to_json(Json &json, const CargoDelivery &value)
{
	json["id"] = value.id;
	json["source"] = value.source;
	json["carrier"] = value.carrier;
	json["target"] = value.target;
	json["from"] = value.from;
	json["cargo"] = value.cargo;
	json["to"] = value.to;
	json["material"] = value.material;
	json["phase"] = value.phase;
	json["amount"] = value.amount;
	json["reserved"] = value.reserved;
	json["report"] = value.report;
	json["report_tick"] = value.report_tick;
	json["collection"] = value.collection;
	json["cancelled"] = value.cancelled;
	json["mission"] = value.mission;
	json["payment"] = value.payment;
	json["payment_amount"] = value.payment_amount;
	json["payment_taken"] = value.payment_taken;
	json["goods_taken"] = value.goods_taken;
	json["paid"] = value.paid;
	json["collected"] = value.collected;
	json["delivered"] = value.delivered;
}
void from_json(const Json &json, CargoDelivery &value)
{
	json.at("id").get_to(value.id);
	json.at("source").get_to(value.source);
	json.at("carrier").get_to(value.carrier);
	json.at("target").get_to(value.target);
	json.at("from").get_to(value.from);
	json.at("cargo").get_to(value.cargo);
	json.at("to").get_to(value.to);
	json.at("material").get_to(value.material);
	json.at("phase").get_to(value.phase);
	json.at("amount").get_to(value.amount);
	json.at("reserved").get_to(value.reserved);
	json.at("report").get_to(value.report);
	json.at("report_tick").get_to(value.report_tick);
	json.at("collection").get_to(value.collection);
	json.at("cancelled").get_to(value.cancelled);
	json.at("mission").get_to(value.mission);
	json.at("payment").get_to(value.payment);
	json.at("payment_amount").get_to(value.payment_amount);
	json.at("payment_taken").get_to(value.payment_taken);
	json.at("goods_taken").get_to(value.goods_taken);
	json.at("paid").get_to(value.paid);
	json.at("collected").get_to(value.collected);
	json.at("delivered").get_to(value.delivered);
}
void to_json(Json &json, const MaterialStoreState &value)
{
	json["stock"] = value.stock;
	json["targets"] = value.targets;
	json["held"] = value.held;
}
void from_json(const Json &json, MaterialStoreState &value)
{
	json.at("stock").get_to(value.stock);
	json.at("targets").get_to(value.targets);
	json.at("held").get_to(value.held);
}
void to_json(Json &json, const MaterialInventory &value)
{
	json["stores"] = value.stores;
	json["revision"] = value.revision;
	json["handling_remainder"] = value.handling_remainder;
	json["work_credit"] = value.work_credit;
	json["authorised_craft"] = value.authorised_craft;
}
void from_json(const Json &json, MaterialInventory &value)
{
	json.at("stores").get_to(value.stores);
	json.at("revision").get_to(value.revision);
	json.at("handling_remainder").get_to(value.handling_remainder);
	json.at("work_credit").get_to(value.work_credit);
	json.at("authorised_craft").get_to(value.authorised_craft);
}
void to_json(Json &json, const MaterialRequest &value)
{
	json["id"] = value.id;
	json["source"] = value.source;
	json["destination"] = value.destination;
	json["from"] = value.from;
	json["to"] = value.to;
	json["material"] = value.material;
	json["remaining"] = value.remaining;
	json["automatic"] = value.automatic;
	json["cargo"] = value.cargo;
	json["service"] = value.service;
}
void from_json(const Json &json, MaterialRequest &value)
{
	json.at("id").get_to(value.id);
	json.at("source").get_to(value.source);
	json.at("destination").get_to(value.destination);
	json.at("from").get_to(value.from);
	json.at("to").get_to(value.to);
	json.at("material").get_to(value.material);
	json.at("remaining").get_to(value.remaining);
	json.at("automatic").get_to(value.automatic);
	json.at("cargo").get_to(value.cargo);
	json.at("service").get_to(value.service);
}
void to_json(Json &json, const MaterialBatch &value)
{
	json["id"] = value.id;
	json["request"] = value.request;
	json["source"] = value.source;
	json["destination"] = value.destination;
	json["location"] = value.location;
	json["from"] = value.from;
	json["to"] = value.to;
	json["compartment"] = value.compartment;
	json["material"] = value.material;
	json["quantity"] = value.quantity;
	json["edge_to"] = value.edge_to;
	json["travel"] = value.travel;
	json["returning"] = value.returning;
	json["crossing"] = value.crossing;
	json["cargo"] = value.cargo;
	json["service"] = value.service;
}
void from_json(const Json &json, MaterialBatch &value)
{
	json.at("id").get_to(value.id);
	json.at("request").get_to(value.request);
	json.at("source").get_to(value.source);
	json.at("destination").get_to(value.destination);
	json.at("location").get_to(value.location);
	json.at("from").get_to(value.from);
	json.at("to").get_to(value.to);
	json.at("compartment").get_to(value.compartment);
	json.at("material").get_to(value.material);
	json.at("quantity").get_to(value.quantity);
	json.at("edge_to").get_to(value.edge_to);
	json.at("travel").get_to(value.travel);
	json.at("returning").get_to(value.returning);
	json.at("crossing").get_to(value.crossing);
	json.at("cargo").get_to(value.cargo);
	json.at("service").get_to(value.service);
}
} // namespace phoenix
