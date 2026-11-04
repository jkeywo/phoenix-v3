module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <utility>
#include <string>
#include <variant>
module phoenix.knowledge.decision_combat;
namespace phoenix
{
namespace
{
Json encode(const CombatCompartmentDecision &v)
{
	Json j = Json::object();
	j["id"] = v.id;
	j["index"] = v.index;
	j["deck"] = v.deck;
	j["hp"] = v.hp;
	j["maximum"] = v.maximum;
	j["disabled"] = v.disabled;
	j["inspected"] = v.inspected;
	j["damaged"] = v.damaged;
	j["x_mm"] = v.x_mm;
	j["y_mm"] = v.y_mm;
	j["half_width_mm"] = v.half_width_mm;
	j["half_length_mm"] = v.half_length_mm;
	return j;
}
Json encode(const CombatTeamDecision &v)
{
	Json j = Json::object();
	j["id"] = v.id;
	j["assignment_manual"] = v.assignment_manual;
	j["assignment_held"] = v.assignment_held;
	j["home"] = v.home;
	j["observed_tick"] = v.observed_tick;
	j["local_complete"] = v.local_complete;
	j["location"] = v.location;
	j["destination"] = v.destination;
	j["travel"] = v.travel;
	j["edge_to"] = v.edge_to;
	j["edge_total"] = v.edge_total;
	return j;
}
Json encode(const CombatArcDecision &v)
{
	Json j = Json::object();
	j["id"] = v.id;
	j["kind"] = v.kind;
	j["bearing_mdeg"] = v.bearing_mdeg;
	j["rest_mdeg"] = v.rest_mdeg;
	j["arc_mdeg"] = v.arc_mdeg;
	j["coverage"] = v.coverage;
	j["label"] = v.label;
	j["family"] = v.family;
	j["active"] = v.active;
	j["disabled"] = v.disabled;
	return j;
}
Json encode(const CombatFaultDecision &v)
{
	Json j = Json::object();
	j["system"] = v.system;
	j["compartment"] = v.compartment;
	j["compartment_index"] = v.compartment_index;
	j["symptom"] = v.symptom;
	j["diagnosed"] = v.diagnosed;
	j["diagnosis"] = v.diagnosis;
	j["destroyed"] = v.destroyed;
	j["work"] = v.work;
	j["required_work"] = v.required_work;
	return j;
}
Json encode(const CombatMagazineDecision &v)
{
	Json j = Json::object();
	j["id"] = v.id;
	j["selected_resource"] = v.selected_resource;
	j["resource_choices"] = v.resource_choices;
	j["resource_labels"] = v.resource_labels;
	j["selectable_resources"] = v.selectable_resources;
	j["stock_revision"] = v.stock_revision;
	j["manual"] = v.manual;
	return j;
}
Json encode(const CombatAmmunitionDecision &v)
{
	Json j = Json::object();
	j["index"] = v.index;
	j["id"] = v.id;
	j["range_mm"] = v.range_mm;
	j["guidance"] = v.guidance;
	j["quantity"] = v.quantity;
	j["use"] = v.use;
	j["available"] = v.available;
	return j;
}
Json encode(const CombatWeaponDecision &v)
{
	Json j = Json::object();
	j["id"] = v.id;
	j["enabled"] = v.enabled;
	j["durable_payload"] = v.durable_payload;
	j["ammunition_free"] = v.ammunition_free;
	j["label"] = v.label;
	j["magazines"] = v.magazines;
	j["selected"] = v.selected;
	j["shots"] = v.shots;
	j["loaded"] = v.loaded;
	j["loaded_resource"] = v.loaded_resource;
	j["loaded_quantity"] = v.loaded_quantity;
	j["loading"] = v.loading;
	j["unloading"] = v.unloading;
	j["load_ticks"] = v.load_ticks;
	j["unload_ticks"] = v.unload_ticks;
	j["mode"] = v.mode;
	j["point_defence"] = v.point_defence;
	j["intercept"] = v.intercept;
	j["ammunition_options"] = Json::array();
	for (const auto &row : v.ammunition_options)
		j["ammunition_options"].push_back(encode(row));
	j["reload_stock_blocked"] = v.reload_stock_blocked;
	return j;
}
Json encode(const CombatShieldDecision &v)
{
	Json j = Json::object();
	j["id"] = v.id;
	j["contribution"] = v.contribution;
	j["effect_id"] = v.effect_id;
	j["component_id"] = v.component_id;
	j["controllable"] = v.controllable;
	j["strength"] = v.strength;
	j["capacity"] = v.capacity;
	j["warmup"] = v.warmup;
	j["warmup_ticks"] = v.warmup_ticks;
	j["arc_mdeg"] = v.arc_mdeg;
	j["bearing_mdeg"] = v.bearing_mdeg;
	j["layer"] = v.layer;
	j["discharging"] = v.discharging;
	return j;
}
} // namespace
Json combat_decision_json(const CombatDecisionSnapshot &v)
{
	Json j{{"destroyed", v.destroyed}};
	j["compartments"] = Json::array();
	for (const auto &row : v.compartments)
		j["compartments"].push_back(encode(row));
	j["teams"] = Json::array();
	for (const auto &row : v.teams)
		j["teams"].push_back(encode(row));
	j["arcs"] = Json::array();
	for (const auto &row : v.arcs)
		j["arcs"].push_back(encode(row));
	j["faults"] = Json::array();
	for (const auto &row : v.faults)
		j["faults"].push_back(encode(row));
	j["shields"] = Json::array();
	for (const auto &row : v.shields)
		j["shields"].push_back(encode(row));
	j["weapons"] = Json::array();
	for (const auto &row : v.weapons)
		j["weapons"].push_back(encode(row));
	j["magazines"] = Json::array();
	for (const auto &row : v.magazines)
		j["magazines"].push_back(encode(row));
	if (v.result)
		j["result"] = *v.result;
	return j;
}

SensorFindingDecision sensor_finding_decision(const Json &data, std::uint64_t observed)
{
	SensorFindingDecision v;
	v.observed = observed;
	v.data_present = !data.is_null();
	if (data.is_object())
	{
		v.classification = data.value("class", std::string{});
		if (data.contains("class"))
			v.identity = SensorIdentityDecision{v.classification, data.value("faction", std::string{}),
			                                    data.value("faction_label", std::string{}),
			                                    data.value("disposition", std::string{})};
		if (data.contains("text") && data.at("text").is_string())
			v.report_findings.push_back({data.at("text"), observed});
	}
	else if (data.is_array())
		for (const auto &row : data)
			if (row.is_object() && row.contains("text") && row.at("text").is_string())
				v.report_findings.push_back({row.at("text"), row.value("observed", std::uint64_t{0})});
	return v;
}
DecisionEvidenceKey decision_evidence_key(const KnowledgeRecord &r)
{
	const auto &p = r.value.provenance;
	return {r.id,
	        r.projection_revision,
	        r.scope,
	        r.owner,
	        r.publication,
	        r.recipient_actor,
	        r.value.schema,
	        r.value.producer,
	        r.value.subject,
	        p.source,
	        p.observed_tick,
	        p.revision,
	        p.uncertainty,
	        p.kind,
	        r.value.withdrawn,
	        r.received_tick,
	        r.expires_tick};
}
DecisionEvidencePayload decode_decision_evidence(const KnowledgeRecordInput &record,
                                                 const CompiledKnowledge &definitions)
{
	if (!record.schema || record.schema > definitions.schemas.size())
		return {};
	const auto &schema = definitions.schemas.at(record.schema - 1);
	DecisionEvidencePayload invalid;
	if (schema.name == "phoenix.contact.report")
		invalid = ContactReportDecision{};
	else if (schema.name == "phoenix.team.inspection")
		invalid = InspectionReportDecision{};
	else if (schema.name.starts_with("phoenix.sensor."))
	{
		SensorReportDecision v;
		v.capability = schema.name.substr(15);
		invalid = std::move(v);
	}

	const auto field = [&](const char *name) -> const KnowledgeValue *
	{
		for (const auto &d : schema.fields)
			if (d.name == name)
			{
				auto at = record.fields.find(d.key);
				return at == record.fields.end() ? nullptr : &at->second;
			}
		return nullptr;
	};
	const auto text = [&](const char *name, std::string fallback = std::string{})
	{
		const auto *v = field(name);
		const auto *t = v ? std::get_if<std::string>(v) : nullptr;
		return t ? *t : fallback;
	};
	const auto number = [&](const char *name)
	{
		const auto *v = field(name);
		const auto *n = v ? std::get_if<std::int64_t>(v) : nullptr;
		return n ? *n : 0LL;
	};
	const auto flag = [&](const char *name)
	{
		const auto *v = field(name);
		const auto *b = v ? std::get_if<bool>(v) : nullptr;
		return b && *b;
	};
	try
	{
		if (schema.name == "phoenix.contact.report")
		{
			if (record.fields.size() != schema.fields.size())
				return invalid;
			ContactReportDecision out;
			auto &b = out.body;
			b.id = record.subject;
			auto icon = radar_icon_named(text("category"));
			if (!icon)
				return invalid;
			b.radar_icon = *icon;
			b.x = number("x");
			b.y = number("y");
			b.vx = number("vx");
			b.vy = number("vy");
			b.heading = number("heading");
			b.altitude = number("altitude");
			b.definition = text("classification");
			b.disposition = text("disposition");
			out.identity_tick = std::max<std::int64_t>(0, number("identity_tick"));
			out.valid = true;
			return out;
		}
		if (schema.name == "phoenix.team.inspection")
		{
			if (record.fields.size() != schema.fields.size())
				return invalid;
			InspectionReportDecision out;
			out.ship = number("ship");
			out.compartment = text("compartment");
			out.medical = flag("medical");
			out.repair = flag("repair");
			out.hp = number("hp");
			out.maximum = number("maximum");
			if (out.compartment.empty() || out.medical == out.repair)
				return invalid;
			const auto faults = Json::parse(text("faults", "[]"), nullptr, false),
			           patients = Json::parse(text("patients", "[]"), nullptr, false);
			if (!faults.is_array() || !patients.is_array() || faults.size() != number("fault_count") ||
			    patients.size() != number("patient_count"))
				return invalid;
			for (const auto &f : faults)
			{
				if (!f.is_object() || f.value("compartment", std::string{}) != out.compartment ||
				    !f.contains("system") || !f.at("system").is_string() || !f.contains("destroyed") ||
				    !f.at("destroyed").is_boolean() || !f.contains("compartment_index") ||
				    !f.at("compartment_index").is_number_integer())
					return invalid;
				CombatFaultDecision v;
				v.system = f.at("system");
				v.compartment = out.compartment;
				v.compartment_index = f.at("compartment_index");
				v.destroyed = f.at("destroyed");
				v.symptom = f.value("symptom", std::string{});
				v.diagnosed = f.value("diagnosed", false);
				v.diagnosis = f.value("diagnosis", std::string{});
				v.work = f.value("work", 0LL);
				v.required_work = f.value("required_work", 0LL);
				out.faults.push_back(std::move(v));
			}
			for (const auto &f : patients)
			{
				if (!f.is_object() || f.value("compartment", std::string{}) != out.compartment || !f.contains("id") ||
				    !f.at("id").is_string() || !f.contains("condition") || !f.at("condition").is_string() ||
				    !f.contains("location") || !f.at("location").is_number_integer() || !f.contains("facility") ||
				    !f.at("facility").is_string())
					return invalid;
				InspectionPatientDecision v;
				v.id = f.at("id");
				v.compartment = out.compartment;
				v.condition = f.at("condition");
				v.location = f.at("location");
				v.facility = f.at("facility");
				v.name = f.value("name", std::string{});
				v.stabilised = f.value("stabilised", false);
				v.escort = f.value("escort", std::string{});
				out.patients.push_back(std::move(v));
			}
			out.valid = true;
			return out;
		}
		if (schema.name.starts_with("phoenix.sensor."))
		{
			SensorReportDecision out;
			out.capability = schema.name.substr(15);
			const auto data = Json::parse(text("payload"), nullptr, false);
			if (data.is_discarded())
				return invalid;
			if (out.capability == "findings")
			{
				if (!data.is_array())
					return invalid;
				for (const auto &f : data)
					if (f.contains("tag") && f.at("tag").is_string())
						out.finding_tags.push_back(f.at("tag").get<std::string>());
				// A tag-only report remains usable by dialogue even when detailed
				// scanner decoding below fails. Scanner readers still require valid.
				invalid = out;
				for (const auto &f : data)
				{
					auto id = f.at("id").get<std::string>();
					auto v = sensor_finding_decision(f, f.at("observed"));
					v.revision = record.provenance.revision;
					v.source = std::to_string(record.provenance.source);
					out.findings["finding:" + id] = std::move(v);
				}
			}
			else
			{
				auto v = sensor_finding_decision(data, record.provenance.observed_tick);
				v.revision = record.provenance.revision;
				v.source = std::to_string(record.provenance.source);
				if (out.capability == "basic_identity" && data.is_object())
					v.classification = data.value("class", std::string{});
				out.findings[out.capability] = std::move(v);
			}
			out.valid = true;
			return out;
		}
	}
	catch (const Json::exception &)
	{
		return invalid;
	}
	catch (const std::bad_variant_access &)
	{
		return invalid;
	}
	return invalid;
}
} // namespace phoenix
