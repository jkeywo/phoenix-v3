module;
#include <nlohmann/json.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module phoenix.state_tree;
export import phoenix.data;

export namespace phoenix::state_tree
{
using Index = std::uint32_t;
inline constexpr Index none = std::numeric_limits<Index>::max();
enum class Context : std::uint8_t
{
	System = 1,
	Scenario = 2,
	Controller = 4,
	Conversation = 8,
	Away = 16,
	Team = 32
};
enum class Type : std::uint8_t
{
	Missing,
	Boolean,
	Integer,
	Text,
	Entity,
	System,
	Console,
	Objective,
	Region,
	Message
};
struct Value
{
	Type type{Type::Missing};
	std::int64_t number{};
	std::string text;
	static Value boolean(bool value);
	static Value integer(std::int64_t value);
	static Value string(std::string value);
	static Value reference(Type type, std::uint64_t identity);
	bool operator==(const Value &) const = default;
};
struct FieldSpec
{
	std::string name;
	Type type{Type::Integer};
	std::uint8_t contexts{3};
	Value initial;
};
struct ActionSpec
{
	std::string name;
	std::uint8_t contexts{3};
	std::vector<Type> arguments;
	bool effect{};
	// Non-effect actions with the same channel and target resolve last-write-wins.
	Index channel{none};
	Index target_argument{none};
	// Optional intent field updated immediately with arguments[value_argument].
	Index intent_field{none};
	Index value_argument{none};
	// Composite conflict key. Empty retains target_argument compatibility.
	std::vector<Index> key_arguments;
	bool capture_context = false;
};
struct Registry
{
	std::vector<FieldSpec> observations;
	std::vector<FieldSpec> intents;
	std::vector<ActionSpec> actions;
};
using Catalog = std::map<std::string, Json>;
enum class Source : std::uint8_t
{
	Literal,
	Observation,
	Intent,
	Memory,
	Elapsed
};
struct Binding
{
	Source source{Source::Literal};
	Index index{none};
	Value literal;
	Type type{Type::Missing};
};
enum class Compare : std::uint8_t
{
	Equal,
	NotEqual,
	Less,
	LessEqual,
	Greater,
	GreaterEqual,
	Present
};
bool compare_values(const Value &left, Compare operation, const Value &right);
struct Condition
{
	Binding left;
	Compare operation{};
	Binding right;
};
enum class TaskKind : std::uint8_t
{
	Memory,
	Intent,
	Action,
	Call
};
struct Task
{
	TaskKind kind{};
	Index id{}, target{none};
	std::vector<Binding> arguments;
	bool once{};
};
struct Transition
{
	Index target{none};
	std::string reason;
	std::vector<Condition> conditions;
};
struct State
{
	std::string name;
	Index parent{none}, initial{none};
	std::vector<Task> entry, running, exit;
	std::vector<Transition> transitions;
};
struct Program
{
	Context context{Context::System};
	Registry registry;
	Index initial{none};
	std::uint32_t budget{4096};
	std::vector<FieldSpec> memory;
	std::map<std::string, Value> parameters;
	std::vector<State> states;
	std::vector<Program> references;
	std::string signature;
};
struct Frame
{
	Index state{none};
	std::uint64_t elapsed{}, activation{};
	std::vector<Index> fired;
};
struct Instance
{
	std::vector<Frame> path;
	std::vector<Value> memory;
	std::map<Index, Instance> calls;
	std::uint64_t next_activation{1};
	bool initialised{}, interrupted{}, resume_pending{};
};
struct Output
{
	Index action{none};
	std::vector<Value> arguments;
	std::uint64_t activation{};
	Index task{none};
	std::vector<Index> call_path;
	std::vector<std::uint64_t> call_activations;
	// Optional emission-time context for typed structured message bindings.
	std::map<std::string, Value> captured_memory, captured_intents, captured_parameters;
	std::uint64_t captured_elapsed = 0;
};
struct Debug
{
	std::vector<std::string> active;
	std::string transition;
	std::vector<std::string> diagnostics;
	std::uint32_t work{};
};
struct Result
{
	std::vector<Output> outputs;
	std::vector<Value> intents;
	Debug debug;
};
// Resolve inheritance/overrides without requiring a finished registry or tasks.
Json resolve_definition(const Json &definition, const Catalog &catalog = {});
Program compile(const Json &definition, const Registry &registry, const Catalog &catalog = {});
Result step(const Program &program, Instance &instance, std::span<const Value> observations, std::uint64_t ticks = 1);
// Interruption runs exit tasks once, cancels child decision continuations, then
// leaves this instance stopped until reset. Already returned effects are untouched.
Result interrupt(const Program &program, Instance &instance, std::span<const Value> observations);
// Restart while retaining the monotonic root activation allocator.
void reset(Instance &instance);
// Resume an interrupted controller, preserving its private memory and allocator.
void resume(Instance &instance);
Json save(const Program &program, const Instance &instance);
Instance load(const Program &program, const Json &continuation);
} // namespace phoenix::state_tree
