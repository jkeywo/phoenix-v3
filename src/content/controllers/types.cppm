module;
#include <set>
#include <cstdint>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

export module phoenix.content.controllers.types;
export import phoenix.data;
export import phoenix.contracts;
export import phoenix.knowledge;
export import phoenix.state_tree;
export import phoenix.gameplay_vocabulary;

export namespace phoenix
{
struct ControllableLayout;
struct ControllerTreeBindings;
struct ControllerCandidateSpec
{
	std::string id, description, deviation;
	state_tree::Program program;
};
struct ControllerActionSpec
{
	std::string id, label, tree;
	std::vector<std::size_t> inputs;
	state_tree::Program program, availability;
};
struct GameplayTreeSummaryBinding
{
	std::string name, channel;
};
struct ControllerTelemetryBinding
{
	std::string name, system, field;
	state_tree::Type type = state_tree::Type::Integer;
};
struct ControllerCommandReadBinding
{
	std::string name, controller, channel;
	state_tree::Type type = state_tree::Type::Integer;
};
struct ControllerContactBinding
{
	std::string name, controller, channel;
};
enum class ControllerOwnField
{
	Entity,
	Console,
	Tick,
	X,
	Y,
	Vx,
	Vy,
	Heading,
	Blocked,
	LeftHeading,
	LateralAvoidance,
	LateralSafe,
	AlertId,
	AlertSeverity,
	Stance,
	FtlReady,
	Visitors,
	LocalWorkComplete,
	CraftX,
	CraftY,
	CraftAltitude,
	CraftSpeed,
	CraftTarget,
	CraftNavigate,
	CraftHeading,
	CraftOrder,
	CraftReserveLow,
	CraftArmed,
	CraftTargetPresent,
	ObjectiveTarget,
	PriorityObjective,
	OwnIdentity,
	Count
};
enum class ControllerRecordField
{
	Available,
	ObservedTick,
	ReceivedTick,
	Source,
	Uncertainty,
	Kind,
	Withdrawn,
	Stale,
	Revision,
	Subject,
	Count
};
enum class ControllerCommandField
{
	Accepted,
	Revision,
	Consumed,
	RequestId,
	Pending,
	Count
};
enum class ControllerInboxField
{
	Available,
	Id,
	Source,
	SourceConsole,
	Schema,
	Subject,
	ObservedTick,
	DeliveredTick,
	Uncertainty,
	Kind,
	Urgency,
	RequestKey,
	Count
};
enum class ControllerContactField
{
	Available,
	Id,
	X,
	Y,
	Vx,
	Vy,
	Heading,
	Speed,
	ObservedTick,
	Uncertainty,
	BearingAvailable,
	BearingMdeg,
	Count
};
enum class ControllerAbilityField
{
	Phase,
	Ready,
	Preparation,
	Recovery,
	Output,
	Blocked,
	Requested,
	Count
};
enum class ControllerProcessField
{
	Phase,
	Requested,
	Running,
	Paused,
	Blocked,
	Activity,
	Work,
	Finite,
	Driver,
	Target,
	Progress,
	RequiredWork,
	Completed,
	Ready,
	CompletionDelivered,
	Count
};
enum class ControllerResultField
{
	Available,
	Success,
	Id,
	Publication,
	Message,
	Reason,
	Count
};
template <typename Field> struct ControllerDestinations
{
	std::array<std::size_t, static_cast<std::size_t>(Field::Count)> values{};
	std::size_t &operator[](Field field)
	{
		return values.at(static_cast<std::size_t>(field));
	}
	std::size_t operator[](Field field) const
	{
		return values.at(static_cast<std::size_t>(field));
	}
};
template <typename Source> struct ControllerRead
{
	Source source;
	std::size_t destination = 0;
};
struct ControllerRecordRead : ControllerRead<GameplayTreeRecordBinding>
{
	ControllerDestinations<ControllerRecordField> metadata;
};
struct ControllerContactRead
{
	ControllerContactBinding source;
	ControllerDestinations<ControllerContactField> fields;
};
struct ControllerAbilityPropertyBinding
{
	std::string property;
	std::size_t destination = 0;
	state_tree::Type type = state_tree::Type::Missing;
};
struct ControllerAbilityBinding
{
	std::string endpoint;
	ControllerDestinations<ControllerAbilityField> fields;
	std::vector<ControllerAbilityPropertyBinding> properties;
};
struct ControllerProcessBinding
{
	std::string endpoint;
	ControllerDestinations<ControllerProcessField> fields;
	std::vector<ControllerAbilityPropertyBinding> properties;
};
struct ControllerCommandBinding
{
	std::string channel;
	std::size_t destination = 0;
	std::vector<std::size_t> arguments;
	ControllerDestinations<ControllerCommandField> metadata;
};
// Derived together with the registry; never persisted. Sources are resolved to
// entity-local indices by simulation setup, destinations are never reconstructed.
struct ControllerTreeBindings
{
	std::string ftl_system;
	std::size_t decision_base = 0, action_base = 0, coordination_base = 0;
	state_tree::Registry registry;
	ControllerDestinations<ControllerOwnField> own;
	ControllerDestinations<ControllerInboxField> inbox;
	ControllerDestinations<ControllerResultField> result;
	std::vector<ControllerCommandBinding> commands;
	std::vector<ControllerRecordRead> records;
	std::vector<ControllerRead<ControllerTelemetryBinding>> telemetry;
	std::vector<ControllerRead<ControllerCommandReadBinding>> command_reads;
	std::vector<ControllerRead<GameplayTreeRecordBinding>> messages;
	std::vector<ControllerContactRead> contacts;
	std::vector<ControllerAbilityBinding> abilities;
	std::vector<ControllerProcessBinding> processes;
};
struct ConsoleSpec
{
	std::string id, station, definition;
	SystemMask controls = 0, feeds = 0;
	std::vector<std::string> controlled_systems, feed_systems;
	bool human_seeking = false;
	std::vector<std::string> human_preferences;
	bool shield_feed = false;
};
struct BridgeChoice
{
	std::string id, label, description;
};
struct AlertCondition : BridgeChoice
{
	float music_gain = 1;
	std::string colour = "#000000", siren, music;
	bool glow = false;
};
struct BridgeDefinition
{
	std::vector<AlertCondition> conditions;
	std::size_t initial = 0;
	std::string alert_console, sidebar_default;
};
struct ConsoleBridgeDefinition
{
	std::vector<BridgeChoice> stances;
	std::size_t initial = 0;
	bool command = false, auto_hide = false, viewscreen = false, objectives = false;
	Json compact;
};
struct ControllerCommandSpec
{
	std::string id;
	OperatingAction action = OperatingAction::Speed;
	OperatingAction physical_input = OperatingAction::Speed;
	KnowledgeType type = KnowledgeType::Integer;
	CommandSemantics semantics = CommandSemantics::Setting;
	std::int64_t initial = 0, minimum = 0, maximum = 0;
	std::vector<std::size_t> consumers;
	std::vector<std::int64_t> initial_arguments;
	bool initial_heading = false;
	std::int64_t arrival_radius = 5000;
	std::string label_key, ui_system; // Optional presentation subject for abstract power intent.
	std::map<std::int64_t, std::string> choices;
	std::string shared_target_controller, shared_target_channel;
	int individual_stars = 1;
	bool hull_priority = false;
	std::string coordinated_group;
};
enum class WidgetDataDestination : std::uint8_t
{
	Main,
	Details,
	Hidden
};
struct ConsoleControllerSpec
{
	std::string id, console, tree;
	std::string policy;
	std::vector<ControllerCommandSpec> commands;
	// Shared logical widget instance, independent of any rendered surface.
	std::string widget;
	std::map<int, std::string> star_levels;
	std::map<int, std::set<std::string>> human_channels;
	std::map<int, std::map<std::string, WidgetDataDestination>> data_placement;
	std::map<std::string, std::vector<std::string>> equipment;
};
struct ConsoleComplexitySpec
{
	std::string id;
	// Profile allocation when a human is available; absence makes every entry AI.
	std::vector<bool> human;
	int stars = 0;
};
struct ControllableLayout
{
	std::vector<ConsoleControllerSpec> controllers;
	std::vector<ConsoleComplexitySpec> profiles;
	std::map<std::string, std::string> complexity_groups;
	std::map<std::string, std::map<std::string, int>> manual_workloads;
	std::map<std::string, std::size_t> default_profiles;
	std::map<std::string, std::size_t> fixed_profiles;
	std::size_t default_profile = 0;
	bool converted = false;
};
} // namespace phoenix
