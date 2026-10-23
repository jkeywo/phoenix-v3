module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

export module phoenix.gameplay_vocabulary;
export import phoenix.state_tree;
export import phoenix.contracts;

export namespace phoenix
{
// Fixed numeric layouts shared by System/scenario registries. Capability and
// permission filtering retains these indices and changes availability, not order.
enum class GameplayTreeObservation : std::uint32_t
{
	SelfEntity,
	SelfSystem,
	SelfConsole,
	Tick,
	Enabled,
	RequestedOutput,
	ActualOutput,
	AvailableOutput,
	StoredEnergy,
	EnergyCapacity,
	IncomingPower,
	UsedPower,
	ReservedPower,
	Heat,
	HeatCapacity,
	PassiveCooling,
	AllocatedCooling,
	ActualCooling,
	CoolantStock,
	CoolantCapacity,
	ThermalStopped,
	ThermalOverride,
	SupplyLimited,
	FlowCut,
	LinkIsolated,
	PositionX,
	PositionY,
	VelocityX,
	VelocityY,
	Heading,
	ObservedSpeed,
	FlightMode,
	BoostActive,
	ImpulsePhase,
	ImpulseProgress,
	MatchSpeedActive,
	MatchedContact,
	MatchedSpeed,
	MatchObservationStale,
	Objective,
	ObjectiveState,
	ObjectiveKnown,
	DestinationRegion,
	DestinationX,
	DestinationY,
	MessageAvailable,
	Message,
	MessageSenderSystem,
	MessageSenderConsole,
	MessageSchema,
	MessageRequestKey,
	MessageInteger,
	MessageBoolean,
	MessageText,
	MessageObservedTick,
	MessageDeliveredTick,
	MessageUncertainty,
	MessageCursor,
	ResultAvailable,
	ResultRequest,
	ResultSucceeded,
	ResultReason,
	ResultEntity,
	ResultObjective,
	ResultMessage,
	ResultPublication,
	ResultInteger,
	ScenarioElapsed,
	EventAvailable,
	EventName,
	EventSource,
	EventInteger,
	EventBoolean,
	EventText,
	RegionActorInside,
	ScenarioFinished,
	SummaryLevel,
	Following,
	FollowStale,
	SelfDefinition,
	Count
};
enum class GameplayTreeIntent : std::uint32_t
{
	Speed,
	Heading,
	Enabled,
	FlightMode,
	LongitudinalThrust,
	LateralThrust,
	Steering,
	Boost,
	Impulse,
	MatchSpeed,
	GeneratorOutput,
	StorageChargeLimit,
	StorageDischargeLimit,
	GroupAllocation,
	LinkIsolation,
	CoolingAllocation,
	ThermalOverride,
	SummaryLevel,
	Count
};
enum class GameplayTreeAction : std::uint32_t
{
	SetSpeed,
	SetHeading,
	SetEnabled,
	SetFlightMode,
	SetLongitudinalThrust,
	SetLateralThrust,
	SetSteering,
	SetBoost,
	SetImpulse,
	SetMatchSpeed,
	SetDestination,
	SetGeneratorOutput,
	SetStorageChargeLimit,
	SetStorageDischargeLimit,
	SetGroupAllocation,
	SetLinkIsolation,
	SetCoolingAllocation,
	SetThermalOverride,
	SetSummaryIntent,
	SendSystemMessage,
	SendConsoleMessage,
	SendStationMessage,
	SendShipMessage,
	ReceiveMessages,
	WithdrawRequest,
	PublishRecord,
	StopPublication,
	SpawnEntity,
	SpawnGroup,
	AddObjective,
	PublishObjective,
	PublishScenarioMessage,
	EmitScenarioEvent,
	FinishScenario,
	StartSituation,
	SetObjectiveStatus,
	StartObjectiveSequence,
	SetFollowContact,
	ApplyShieldEffect,
	RefreshShieldEffect,
	RemoveShieldEffect,
	ApplyModifierEffect,
	RefreshModifierEffect,
	RemoveModifierEffect,
	Count
};
enum class GameplayTreeRecordScope : std::uint8_t
{
	Console,
	Ship,
	Scenario
};
struct GameplayTreeRecordBinding
{
	// Typed slot name used by tree {observation:"..."} bindings.
	std::string name;
	GameplayTreeRecordScope scope{GameplayTreeRecordScope::Console};
	std::string schema, field;
	// Name of a declared fitted/context parameter selecting the subject. Empty
	// means the owning entity. Resolution and read permission occur at loading.
	std::string subject_parameter;
	state_tree::Type type{state_tree::Type::Integer};
	std::uint8_t contexts{1};
};
state_tree::Registry gameplay_tree_registry(state_tree::Context context);
state_tree::Registry gameplay_tree_registry(state_tree::Context context,
                                            std::span<const GameplayTreeRecordBinding> records);
// Authoring metadata includes enum indices, types, units, argument labels,
// context masks and effect/intent semantics; it contains no runtime observations.
Json gameplay_tree_vocabulary();
Json gameplay_tree_vocabulary(std::span<const GameplayTreeRecordBinding> records);
} // namespace phoenix
