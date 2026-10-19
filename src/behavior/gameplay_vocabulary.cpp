module;
#include <optional>
#include <variant>
#include <map>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.gameplay_vocabulary;

namespace phoenix
{
namespace
{
using state_tree::Index;
using state_tree::none;
using state_tree::Type;
constexpr std::uint8_t system_context = 1, scenario_context = 2, both_contexts = 3;
struct ObservationDefinition
{
	GameplayTreeObservation id;
	const char *name;
	Type type;
	std::uint8_t contexts;
	const char *units;
};
const std::vector<ObservationDefinition> &observation_definitions()
{
	static const std::vector<ObservationDefinition> result{
	    {GameplayTreeObservation::SelfEntity, "self.entity", Type::Entity, both_contexts, "identity"},
	    {GameplayTreeObservation::SelfSystem, "self.system", Type::System, system_context, "identity"},
	    {GameplayTreeObservation::SelfConsole, "self.console", Type::Console, system_context, "identity"},
	    {GameplayTreeObservation::Tick, "tick", Type::Integer, both_contexts, "ticks"},
	    {GameplayTreeObservation::Enabled, "system.enabled", Type::Boolean, system_context, ""},
	    {GameplayTreeObservation::RequestedOutput, "system.requested", Type::Integer, system_context,
	     "equipment-defined"},
	    {GameplayTreeObservation::ActualOutput, "system.actual", Type::Integer, system_context, "equipment-defined"},
	    {GameplayTreeObservation::AvailableOutput, "system.available", Type::Integer, system_context,
	     "equipment-defined"},
	    {GameplayTreeObservation::StoredEnergy, "power.stored", Type::Integer, system_context, "energy-quanta"},
	    {GameplayTreeObservation::EnergyCapacity, "power.capacity", Type::Integer, system_context, "energy-quanta"},
	    {GameplayTreeObservation::IncomingPower, "power.incoming", Type::Integer, system_context,
	     "energy-quanta-per-tick"},
	    {GameplayTreeObservation::UsedPower, "power.used", Type::Integer, system_context, "energy-quanta-per-tick"},
	    {GameplayTreeObservation::ReservedPower, "power.reserved", Type::Integer, system_context,
	     "energy-quanta-per-tick"},
	    {GameplayTreeObservation::Heat, "thermal.heat", Type::Integer, system_context, "heat-quanta"},
	    {GameplayTreeObservation::HeatCapacity, "thermal.capacity", Type::Integer, system_context, "heat-quanta"},
	    {GameplayTreeObservation::PassiveCooling, "thermal.passive", Type::Integer, system_context,
	     "heat-quanta-per-tick"},
	    {GameplayTreeObservation::AllocatedCooling, "thermal.allocated", Type::Integer, system_context,
	     "heat-quanta-per-tick"},
	    {GameplayTreeObservation::ActualCooling, "thermal.removed", Type::Integer, system_context,
	     "heat-quanta-per-tick"},
	    {GameplayTreeObservation::CoolantStock, "coolant.stock", Type::Integer, system_context, "coolant-quanta"},
	    {GameplayTreeObservation::CoolantCapacity, "coolant.capacity", Type::Integer, system_context, "coolant-quanta"},
	    {GameplayTreeObservation::ThermalStopped, "thermal.stopped", Type::Boolean, system_context, ""},
	    {GameplayTreeObservation::ThermalOverride, "thermal.override", Type::Boolean, system_context, ""},
	    {GameplayTreeObservation::SupplyLimited, "power.limited", Type::Boolean, system_context, ""},
	    {GameplayTreeObservation::FlowCut, "power.cut", Type::Boolean, system_context, ""},
	    {GameplayTreeObservation::LinkIsolated, "power.isolated", Type::Boolean, system_context, ""},
	    {GameplayTreeObservation::PositionX, "motion.x_mm", Type::Integer, both_contexts, "millimetres"},
	    {GameplayTreeObservation::PositionY, "motion.y_mm", Type::Integer, both_contexts, "millimetres"},
	    {GameplayTreeObservation::VelocityX, "motion.vx_mm_s", Type::Integer, both_contexts, "millimetres-per-second"},
	    {GameplayTreeObservation::VelocityY, "motion.vy_mm_s", Type::Integer, both_contexts, "millimetres-per-second"},
	    {GameplayTreeObservation::Heading, "motion.heading_mdeg", Type::Integer, both_contexts, "millidegrees"},
	    {GameplayTreeObservation::ObservedSpeed, "motion.speed_mm_s", Type::Integer, system_context,
	     "millimetres-per-second"},
	    {GameplayTreeObservation::FlightMode, "flight.mode", Type::Integer, system_context, "0-assisted-1-manual"},
	    {GameplayTreeObservation::BoostActive, "flight.boost", Type::Boolean, system_context, ""},
	    {GameplayTreeObservation::ImpulsePhase, "flight.impulse_phase", Type::Integer, system_context,
	     "0-idle-1-charging-2-active"},
	    {GameplayTreeObservation::ImpulseProgress, "flight.impulse_progress", Type::Integer, system_context,
	     "permille"},
	    {GameplayTreeObservation::MatchSpeedActive, "flight.match_active", Type::Boolean, system_context, ""},
	    {GameplayTreeObservation::MatchedContact, "flight.match_contact", Type::Entity, system_context, "identity"},
	    {GameplayTreeObservation::MatchedSpeed, "flight.match_speed_mm_s", Type::Integer, system_context,
	     "millimetres-per-second"},
	    {GameplayTreeObservation::MatchObservationStale, "flight.match_stale", Type::Boolean, system_context, ""},
	    {GameplayTreeObservation::Objective, "objective.id", Type::Objective, both_contexts, "identity"},
	    {GameplayTreeObservation::ObjectiveState, "objective.state", Type::Integer, both_contexts,
	     "0-pending-1-active-2-completed-3-failed-4-cancelled"},
	    {GameplayTreeObservation::ObjectiveKnown, "objective.known", Type::Boolean, both_contexts, ""},
	    {GameplayTreeObservation::DestinationRegion, "objective.region", Type::Region, both_contexts, "identity"},
	    {GameplayTreeObservation::DestinationX, "objective.x_mm", Type::Integer, both_contexts, "millimetres"},
	    {GameplayTreeObservation::DestinationY, "objective.y_mm", Type::Integer, both_contexts, "millimetres"},
	    {GameplayTreeObservation::MessageAvailable, "message.available", Type::Boolean, system_context, ""},
	    {GameplayTreeObservation::Message, "message.id", Type::Message, system_context, "identity"},
	    {GameplayTreeObservation::MessageSenderSystem, "message.sender_system", Type::System, system_context,
	     "identity"},
	    {GameplayTreeObservation::MessageSenderConsole, "message.sender_console", Type::Console, system_context,
	     "identity"},
	    {GameplayTreeObservation::MessageSchema, "message.schema", Type::Text, system_context, "schema-id"},
	    {GameplayTreeObservation::MessageRequestKey, "message.request_key", Type::Text, system_context, "request-key"},
	    {GameplayTreeObservation::MessageInteger, "message.integer", Type::Integer, system_context, "schema-defined"},
	    {GameplayTreeObservation::MessageBoolean, "message.boolean", Type::Boolean, system_context, ""},
	    {GameplayTreeObservation::MessageText, "message.text", Type::Text, system_context, "schema-defined"},
	    {GameplayTreeObservation::MessageObservedTick, "message.observed_tick", Type::Integer, system_context, "ticks"},
	    {GameplayTreeObservation::MessageDeliveredTick, "message.delivered_tick", Type::Integer, system_context,
	     "ticks"},
	    {GameplayTreeObservation::MessageUncertainty, "message.uncertainty", Type::Integer, system_context,
	     "schema-defined"},
	    {GameplayTreeObservation::MessageCursor, "message.cursor", Type::Integer, system_context, "delivery-sequence"},
	    {GameplayTreeObservation::ResultAvailable, "result.available", Type::Boolean, both_contexts, ""},
	    {GameplayTreeObservation::ResultRequest, "result.request", Type::Text, both_contexts, "request-key"},
	    {GameplayTreeObservation::ResultSucceeded, "result.succeeded", Type::Boolean, both_contexts, ""},
	    {GameplayTreeObservation::ResultReason, "result.reason", Type::Text, both_contexts, "diagnostic-key"},
	    {GameplayTreeObservation::ResultEntity, "result.entity", Type::Entity, both_contexts, "identity"},
	    {GameplayTreeObservation::ResultObjective, "result.objective", Type::Objective, both_contexts, "identity"},
	    {GameplayTreeObservation::ResultMessage, "result.message", Type::Message, both_contexts, "identity"},
	    {GameplayTreeObservation::ResultPublication, "result.publication", Type::Integer, both_contexts,
	     "publication-identity"},
	    {GameplayTreeObservation::ResultInteger, "result.integer", Type::Integer, both_contexts, "action-defined"},
	    {GameplayTreeObservation::ScenarioElapsed, "scenario.elapsed", Type::Integer, scenario_context, "ticks"},
	    {GameplayTreeObservation::EventAvailable, "event.available", Type::Boolean, scenario_context, ""},
	    {GameplayTreeObservation::EventName, "event.name", Type::Text, scenario_context, "event-id"},
	    {GameplayTreeObservation::EventSource, "event.source", Type::Integer, scenario_context, "situation-identity"},
	    {GameplayTreeObservation::EventInteger, "event.integer", Type::Integer, scenario_context, "contract-defined"},
	    {GameplayTreeObservation::EventBoolean, "event.boolean", Type::Boolean, scenario_context, ""},
	    {GameplayTreeObservation::EventText, "event.text", Type::Text, scenario_context, "contract-defined"},
	    {GameplayTreeObservation::RegionActorInside, "region.actor_inside", Type::Boolean, scenario_context, ""},
	    {GameplayTreeObservation::ScenarioFinished, "scenario.finished", Type::Boolean, scenario_context, ""},
	    {GameplayTreeObservation::SummaryLevel, "summary.level", Type::Integer, system_context,
	     "authored-intent-level"}};
	return result;
}
struct IntentDefinition
{
	GameplayTreeIntent id;
	const char *name;
	Type type;
};
const std::vector<IntentDefinition> &intent_definitions()
{
	static const std::vector<IntentDefinition> result{
	    {GameplayTreeIntent::Speed, "speed_mm_s", Type::Integer},
	    {GameplayTreeIntent::Heading, "heading_mdeg", Type::Integer},
	    {GameplayTreeIntent::Enabled, "enabled", Type::Boolean},
	    {GameplayTreeIntent::FlightMode, "flight_mode", Type::Integer},
	    {GameplayTreeIntent::LongitudinalThrust, "longitudinal_permille", Type::Integer},
	    {GameplayTreeIntent::LateralThrust, "lateral_permille", Type::Integer},
	    {GameplayTreeIntent::Steering, "steering_permille", Type::Integer},
	    {GameplayTreeIntent::Boost, "boost", Type::Boolean},
	    {GameplayTreeIntent::Impulse, "impulse", Type::Boolean},
	    {GameplayTreeIntent::MatchSpeed, "match_speed", Type::Boolean},
	    {GameplayTreeIntent::GeneratorOutput, "generator_permille", Type::Integer},
	    {GameplayTreeIntent::StorageChargeLimit, "charge_limit", Type::Integer},
	    {GameplayTreeIntent::StorageDischargeLimit, "discharge_limit", Type::Integer},
	    {GameplayTreeIntent::GroupAllocation, "group_allocation", Type::Integer},
	    {GameplayTreeIntent::LinkIsolation, "link_isolation", Type::Boolean},
	    {GameplayTreeIntent::CoolingAllocation, "cooling_allocation", Type::Integer},
	    {GameplayTreeIntent::ThermalOverride, "thermal_override", Type::Boolean},
	    {GameplayTreeIntent::SummaryLevel, "summary_level", Type::Integer}};
	return result;
}
struct ArgumentDefinition
{
	const char *name;
	Type type;
};
struct ActionDefinition
{
	GameplayTreeAction id;
	const char *name;
	std::uint8_t contexts;
	std::vector<ArgumentDefinition> arguments;
	bool effect{};
	GameplayTreeIntent intent{GameplayTreeIntent::Count};
	Index value_argument{none};
	std::vector<Index> keys;
};
const std::vector<ActionDefinition> &action_definitions()
{
	static const std::vector<ActionDefinition> result{
	    {GameplayTreeAction::SetSpeed,
	     "set_speed",
	     system_context,
	     {{"system", Type::System}, {"speed_mm_s", Type::Integer}},
	     false,
	     GameplayTreeIntent::Speed,
	     1,
	     {0}},
	    {GameplayTreeAction::SetHeading,
	     "set_heading",
	     system_context,
	     {{"system", Type::System}, {"heading_mdeg", Type::Integer}},
	     false,
	     GameplayTreeIntent::Heading,
	     1,
	     {0}},
	    {GameplayTreeAction::SetEnabled,
	     "set_enabled",
	     system_context,
	     {{"system", Type::System}, {"enabled", Type::Boolean}},
	     false,
	     GameplayTreeIntent::Enabled,
	     1,
	     {0}},
	    {GameplayTreeAction::SetFlightMode,
	     "set_flight_mode",
	     system_context,
	     {{"system", Type::System}, {"mode", Type::Integer}},
	     false,
	     GameplayTreeIntent::FlightMode,
	     1,
	     {0}},
	    {GameplayTreeAction::SetLongitudinalThrust,
	     "set_longitudinal_thrust",
	     system_context,
	     {{"system", Type::System}, {"permille", Type::Integer}},
	     false,
	     GameplayTreeIntent::LongitudinalThrust,
	     1,
	     {0}},
	    {GameplayTreeAction::SetLateralThrust,
	     "set_lateral_thrust",
	     system_context,
	     {{"system", Type::System}, {"permille", Type::Integer}},
	     false,
	     GameplayTreeIntent::LateralThrust,
	     1,
	     {0}},
	    {GameplayTreeAction::SetSteering,
	     "set_steering",
	     system_context,
	     {{"system", Type::System}, {"permille", Type::Integer}},
	     false,
	     GameplayTreeIntent::Steering,
	     1,
	     {0}},
	    {GameplayTreeAction::SetBoost,
	     "set_boost",
	     system_context,
	     {{"system", Type::System}, {"active", Type::Boolean}},
	     false,
	     GameplayTreeIntent::Boost,
	     1,
	     {0}},
	    {GameplayTreeAction::SetImpulse,
	     "set_impulse",
	     system_context,
	     {{"system", Type::System}, {"active", Type::Boolean}},
	     false,
	     GameplayTreeIntent::Impulse,
	     1,
	     {0}},
	    {GameplayTreeAction::SetMatchSpeed,
	     "set_match_speed",
	     system_context,
	     {{"system", Type::System}, {"contact", Type::Entity}, {"active", Type::Boolean}},
	     false,
	     GameplayTreeIntent::MatchSpeed,
	     2,
	     {0}},
	    {GameplayTreeAction::SetDestination,
	     "set_destination",
	     system_context,
	     {{"system", Type::System}, {"region", Type::Region}, {"x_mm", Type::Integer}, {"y_mm", Type::Integer}},
	     false,
	     GameplayTreeIntent::Count,
	     none,
	     {0}},
	    {GameplayTreeAction::SetGeneratorOutput,
	     "set_generator_output",
	     system_context,
	     {{"system", Type::System}, {"permille", Type::Integer}},
	     false,
	     GameplayTreeIntent::GeneratorOutput,
	     1,
	     {0}},
	    {GameplayTreeAction::SetStorageChargeLimit,
	     "set_storage_charge_limit",
	     system_context,
	     {{"system", Type::System}, {"store", Type::Text}, {"limit", Type::Integer}},
	     false,
	     GameplayTreeIntent::StorageChargeLimit,
	     2,
	     {0, 1}},
	    {GameplayTreeAction::SetStorageDischargeLimit,
	     "set_storage_discharge_limit",
	     system_context,
	     {{"system", Type::System}, {"store", Type::Text}, {"limit", Type::Integer}},
	     false,
	     GameplayTreeIntent::StorageDischargeLimit,
	     2,
	     {0, 1}},
	    {GameplayTreeAction::SetGroupAllocation,
	     "set_group_allocation",
	     system_context,
	     {{"system", Type::System}, {"group", Type::Text}, {"allocation", Type::Integer}},
	     false,
	     GameplayTreeIntent::GroupAllocation,
	     2,
	     {0, 1}},
	    {GameplayTreeAction::SetLinkIsolation,
	     "set_link_isolation",
	     system_context,
	     {{"system", Type::System}, {"link", Type::Text}, {"isolated", Type::Boolean}},
	     false,
	     GameplayTreeIntent::LinkIsolation,
	     2,
	     {0, 1}},
	    {GameplayTreeAction::SetCoolingAllocation,
	     "set_cooling_allocation",
	     system_context,
	     {{"system", Type::System}, {"consumer", Type::Text}, {"allocation", Type::Integer}},
	     false,
	     GameplayTreeIntent::CoolingAllocation,
	     2,
	     {0, 1}},
	    {GameplayTreeAction::SetThermalOverride,
	     "set_thermal_override",
	     system_context,
	     {{"system", Type::System}, {"active", Type::Boolean}},
	     false,
	     GameplayTreeIntent::ThermalOverride,
	     1,
	     {0}},
	    {GameplayTreeAction::SetSummaryIntent,
	     "set_summary_intent",
	     system_context,
	     {{"system", Type::System}, {"channel", Type::Text}, {"level", Type::Integer}},
	     false,
	     GameplayTreeIntent::SummaryLevel,
	     2,
	     {0, 1}},
	    {GameplayTreeAction::SendSystemMessage,
	     "send_system_message",
	     system_context,
	     {{"sender", Type::System},
	      {"destination", Type::System},
	      {"template", Type::Text},
	      {"subject", Type::Entity},
	      {"amount", Type::Integer},
	      {"flag", Type::Boolean},
	      {"detail", Type::Text},
	      {"request_key", Type::Text},
	      {"delay_ticks", Type::Integer},
	      {"lifetime_ticks", Type::Integer},
	      {"urgency", Type::Integer}},
	     true},
	    {GameplayTreeAction::SendConsoleMessage,
	     "send_console_message",
	     system_context,
	     {{"sender", Type::System},
	      {"destination", Type::Console},
	      {"template", Type::Text},
	      {"subject", Type::Entity},
	      {"amount", Type::Integer},
	      {"flag", Type::Boolean},
	      {"detail", Type::Text},
	      {"request_key", Type::Text},
	      {"delay_ticks", Type::Integer},
	      {"lifetime_ticks", Type::Integer},
	      {"urgency", Type::Integer}},
	     true},
	    {GameplayTreeAction::SendStationMessage,
	     "send_station_message",
	     system_context,
	     {{"sender", Type::System},
	      {"station", Type::Integer},
	      {"template", Type::Text},
	      {"subject", Type::Entity},
	      {"amount", Type::Integer},
	      {"flag", Type::Boolean},
	      {"detail", Type::Text},
	      {"request_key", Type::Text},
	      {"delay_ticks", Type::Integer},
	      {"lifetime_ticks", Type::Integer},
	      {"urgency", Type::Integer}},
	     true},
	    {GameplayTreeAction::SendShipMessage,
	     "send_ship_message",
	     system_context,
	     {{"sender", Type::System},
	      {"ship", Type::Entity},
	      {"template", Type::Text},
	      {"subject", Type::Entity},
	      {"amount", Type::Integer},
	      {"flag", Type::Boolean},
	      {"detail", Type::Text},
	      {"request_key", Type::Text},
	      {"delay_ticks", Type::Integer},
	      {"lifetime_ticks", Type::Integer},
	      {"urgency", Type::Integer}},
	     true},
	    {GameplayTreeAction::ReceiveMessages,
	     "receive_messages",
	     system_context,
	     {{"receiver", Type::System}, {"through_message", Type::Message}},
	     true},
	    {GameplayTreeAction::WithdrawRequest,
	     "withdraw_request",
	     system_context,
	     {{"sender", Type::System}, {"request_key", Type::Text}},
	     true},
	    {GameplayTreeAction::PublishRecord,
	     "publish_record",
	     system_context,
	     {{"console", Type::Console},
	      {"publication", Type::Text},
	      {"subject", Type::Entity},
	      {"live", Type::Boolean},
	      {"request_key", Type::Text}},
	     true},
	    {GameplayTreeAction::StopPublication,
	     "stop_publication",
	     system_context,
	     {{"console", Type::Console}, {"publication_id", Type::Integer}},
	     true},
	    {GameplayTreeAction::SpawnEntity,
	     "spawn_entity",
	     scenario_context,
	     {{"definition", Type::Text},
	      {"spawn_point", Type::Text},
	      {"x_mm", Type::Integer},
	      {"y_mm", Type::Integer},
	      {"heading_mdeg", Type::Integer},
	      {"handle", Type::Text}},
	     true},
	    {GameplayTreeAction::SpawnGroup,
	     "spawn_group",
	     scenario_context,
	     {{"group", Type::Text}, {"handle", Type::Text}},
	     true},
	    {GameplayTreeAction::AddObjective,
	     "add_objective",
	     scenario_context,
	     {{"definition", Type::Text}, {"actor", Type::Entity}, {"handle", Type::Text}},
	     true},
	    {GameplayTreeAction::PublishObjective,
	     "publish_objective",
	     scenario_context,
	     {{"objective", Type::Objective}},
	     true},
	    {GameplayTreeAction::PublishScenarioMessage,
	     "publish_scenario_message",
	     scenario_context,
	     {{"publication", Type::Text},
	      {"ship", Type::Entity},
	      {"amount", Type::Integer},
	      {"flag", Type::Boolean},
	      {"detail", Type::Text}},
	     true},
	    {GameplayTreeAction::EmitScenarioEvent,
	     "emit_scenario_event",
	     scenario_context,
	     {{"event", Type::Text}, {"amount", Type::Integer}, {"flag", Type::Boolean}, {"detail", Type::Text}},
	     true},
	    {GameplayTreeAction::FinishScenario,
	     "finish_scenario",
	     scenario_context,
	     {{"success", Type::Boolean}, {"reason", Type::Text}},
	     true},
	    {GameplayTreeAction::StartSituation,
	     "start_situation",
	     scenario_context,
	     {{"configuration", Type::Text}, {"name", Type::Text}},
	     true},
	    {GameplayTreeAction::SetObjectiveStatus,
	     "set_objective_status",
	     scenario_context,
	     {{"objective", Type::Objective}, {"status", Type::Integer}},
	     true}};
	return result;
}
const char *type_name(Type type)
{
	switch (type)
	{
	case Type::Boolean:
		return "boolean";
	case Type::Integer:
		return "integer";
	case Type::Text:
		return "text";
	case Type::Entity:
		return "entity";
	case Type::System:
		return "system";
	case Type::Console:
		return "console";
	case Type::Objective:
		return "objective";
	case Type::Region:
		return "region";
	case Type::Message:
		return "message";
	case Type::Missing:
		return "missing";
	}
	return "missing";
}
void validate_layout()
{
	if (observation_definitions().size() != static_cast<Index>(GameplayTreeObservation::Count) ||
	    intent_definitions().size() != static_cast<Index>(GameplayTreeIntent::Count) ||
	    action_definitions().size() != static_cast<Index>(GameplayTreeAction::Count))
		throw std::runtime_error("gameplay tree vocabulary: incomplete fixed layout");
	for (Index i = 0; i < observation_definitions().size(); ++i)
		if (static_cast<Index>(observation_definitions()[i].id) != i)
			throw std::runtime_error("gameplay tree vocabulary: observation ordering");
	for (Index i = 0; i < intent_definitions().size(); ++i)
		if (static_cast<Index>(intent_definitions()[i].id) != i)
			throw std::runtime_error("gameplay tree vocabulary: intent ordering");
	for (Index i = 0; i < action_definitions().size(); ++i)
		if (static_cast<Index>(action_definitions()[i].id) != i)
			throw std::runtime_error("gameplay tree vocabulary: action ordering");
}
void validate_records(std::span<const GameplayTreeRecordBinding> records)
{
	if (records.size() + observation_definitions().size() > 4096)
		throw std::runtime_error("gameplay tree vocabulary: too many record bindings");
	std::vector<std::string> names;
	for (const auto &field : observation_definitions())
		names.push_back(field.name);
	for (const auto &record : records)
	{
		if (!valid_identifier(record.name) || !valid_identifier(record.schema) || !valid_identifier(record.field) ||
		    (!record.subject_parameter.empty() && !valid_identifier(record.subject_parameter)) ||
		    record.type <= Type::Missing || record.type > Type::Message || !record.contexts || record.contexts > 3 ||
		    (record.scope == GameplayTreeRecordScope::Scenario && record.contexts != scenario_context) ||
		    static_cast<unsigned>(record.scope) > static_cast<unsigned>(GameplayTreeRecordScope::Scenario))
			throw std::runtime_error("gameplay tree vocabulary: invalid record binding '" + record.name + "'");
		for (const auto &name : names)
			if (name == record.name)
				throw std::runtime_error("gameplay tree vocabulary: duplicate record binding '" + name + "'");
		names.push_back(record.name);
	}
}
} // namespace

state_tree::Registry gameplay_tree_registry(state_tree::Context context)
{
	return gameplay_tree_registry(context, {});
}
state_tree::Registry gameplay_tree_registry(state_tree::Context context,
                                            std::span<const GameplayTreeRecordBinding> records)
{
	if (context != state_tree::Context::System && context != state_tree::Context::Scenario)
		throw std::runtime_error("gameplay tree vocabulary: invalid context");
	validate_layout();
	validate_records(records);
	state_tree::Registry result;
	for (const auto &field : observation_definitions())
		result.observations.push_back({field.name, field.type, field.contexts, {}});
	for (const auto &field : records)
		result.observations.push_back({field.name, field.type, field.contexts, {}});
	for (const auto &field : intent_definitions())
		result.intents.push_back({field.name, field.type, system_context, {}});
	for (const auto &action : action_definitions())
	{
		state_tree::ActionSpec spec;
		spec.name = action.name;
		spec.contexts = action.contexts;
		spec.effect = action.effect;
		spec.channel = static_cast<Index>(action.id);
		for (const auto &argument : action.arguments)
			spec.arguments.push_back(argument.type);
		if (!action.keys.empty())
			spec.target_argument = action.keys.front();
		spec.key_arguments = action.keys;
		if (action.intent != GameplayTreeIntent::Count)
		{
			spec.intent_field = static_cast<Index>(action.intent);
			spec.value_argument = action.value_argument;
		}
		result.actions.push_back(std::move(spec));
	}
	return result;
}
Json gameplay_tree_vocabulary()
{
	return gameplay_tree_vocabulary({});
}
Json gameplay_tree_vocabulary(std::span<const GameplayTreeRecordBinding> records)
{
	validate_layout();
	validate_records(records);
	Json result{
	    {"version", 1}, {"observations", Json::array()}, {"intents", Json::array()}, {"actions", Json::array()}};
	for (const auto &field : observation_definitions())
		result["observations"].push_back(Json{{"index", static_cast<Index>(field.id)},
		                                      {"name", field.name},
		                                      {"type", type_name(field.type)},
		                                      {"contexts", field.contexts},
		                                      {"units", field.units},
		                                      {"source", "permitted-observation"}});
	for (const auto &field : records)
		result["observations"].push_back(Json{{"index", result["observations"].size()},
		                                      {"name", field.name},
		                                      {"type", type_name(field.type)},
		                                      {"contexts", field.contexts},
		                                      {"source", "record-binding"},
		                                      {"scope", field.scope == GameplayTreeRecordScope::Console ? "console"
		                                                : field.scope == GameplayTreeRecordScope::Ship  ? "ship"
		                                                                                                : "scenario"},
		                                      {"schema", field.schema},
		                                      {"field", field.field},
		                                      {"subject_parameter", field.subject_parameter}});
	for (const auto &field : intent_definitions())
		result["intents"].push_back(Json{{"index", static_cast<Index>(field.id)},
		                                 {"name", field.name},
		                                 {"type", type_name(field.type)},
		                                 {"contexts", system_context}});
	for (const auto &action : action_definitions())
	{
		Json args = Json::array();
		for (const auto &argument : action.arguments)
			args.push_back(Json{{"name", argument.name}, {"type", type_name(argument.type)}});
		Json spec{{"index", static_cast<Index>(action.id)},
		          {"name", action.name},
		          {"contexts", action.contexts},
		          {"arguments", args},
		          {"effect", action.effect},
		          {"channel", static_cast<Index>(action.id)},
		          {"key_arguments", action.keys}};
		if (action.intent != GameplayTreeIntent::Count)
		{
			spec["intent_field"] = static_cast<Index>(action.intent);
			spec["value_argument"] = action.value_argument;
		}
		result["actions"].push_back(std::move(spec));
	}
	return result;
}
} // namespace phoenix
