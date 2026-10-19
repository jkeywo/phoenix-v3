module;
#include <optional>
#include <variant>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.state_tree;

namespace phoenix::state_tree
{
Value Value::boolean(bool value)
{
	return {Type::Boolean, value ? 1 : 0, {}};
}
Value Value::integer(std::int64_t value)
{
	return {Type::Integer, value, {}};
}
Value Value::string(std::string value)
{
	return {Type::Text, 0, std::move(value)};
}
Value Value::reference(Type type, std::uint64_t identity)
{
	if (type < Type::Entity || type > Type::Message || identity == 0 ||
	    identity > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
		throw std::runtime_error("state tree: invalid typed identity");
	return {type, static_cast<std::int64_t>(identity), {}};
}
namespace
{
[[noreturn]] void fail(const std::string &message)
{
	throw std::runtime_error("state tree: " + message);
}
Type parse_type(const std::string &text)
{
	static const std::vector<std::string> names{"missing", "boolean", "integer",   "text",   "entity",
	                                            "system",  "console", "objective", "region", "message"};
	const auto found = std::find(names.begin(), names.end(), text);
	if (found == names.end() || found == names.begin())
		fail("unsupported type '" + text + "'");
	return static_cast<Type>(found - names.begin());
}
bool valid_value(const Value &value, Type type, bool missing = false)
{
	if (missing && value.type == Type::Missing)
		return true;
	if (value.type != type || type == Type::Missing)
		return false;
	if (type == Type::Boolean)
		return value.text.empty() && (value.number == 0 || value.number == 1);
	if (type == Type::Text)
		return value.number == 0 && value.text.size() <= 4096;
	return value.text.empty() && (type == Type::Integer || value.number > 0);
}
Value value_from(const Json &json)
{
	if (json.is_boolean())
		return Value::boolean(json.get<bool>());
	if (json.is_number_integer())
	{
		if (json.is_number_unsigned() &&
		    json.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
			fail("integer literal out of signed range");
		return Value::integer(json.get<std::int64_t>());
	}
	if (json.is_string())
	{
		const auto value = json.get<std::string>();
		if (value.size() > 4096)
			fail("text literal exceeds 4096 bytes");
		return Value::string(value);
	}
	if (json.is_object() && json.contains("type") && json.contains("id"))
		return Value::reference(parse_type(json.at("type").get<std::string>()), json.at("id").get<std::uint64_t>());
	fail("literal must be boolean, signed integer, text or typed identity");
}
Json value_json(const Value &value)
{
	return Json{{"type", static_cast<unsigned>(value.type)}, {"number", value.number}, {"text", value.text}};
}
Index field_index(const std::vector<FieldSpec> &fields, const std::string &name)
{
	for (Index i = 0; i < fields.size(); ++i)
		if (fields[i].name == name)
			return i;
	fail("unresolved field '" + name + "'");
}
void field_context(const FieldSpec &field, Context context)
{
	if (!(field.contexts & static_cast<unsigned>(context)))
		fail("field outside permitted context: " + field.name);
}
Json resolve(const Json &source, const Catalog &catalog, std::vector<std::string> &stack)
{
	if (!source.is_object())
		fail("definition must be an object");
	Json result = Json::object();
	if (source.contains("extends"))
	{
		const auto name = source.at("extends").get<std::string>();
		if (std::find(stack.begin(), stack.end(), name) != stack.end() || stack.size() >= 32)
			fail("inheritance cycle/depth at '" + name + "'");
		const auto found = catalog.find(name);
		if (found == catalog.end())
			fail("unresolved inherited tree '" + name + "'");
		stack.push_back(name);
		result = resolve(found->second, catalog, stack);
		stack.pop_back();
	}
	for (auto it = source.begin(); it != source.end(); ++it)
	{
		if (it.key() == "extends" || it.key() == "overrides")
			continue;
		if ((it.key() == "parameters" || it.key() == "arguments") && result.contains(it.key()))
			result[it.key()].update(it.value());
		else
			result[it.key()] = it.value();
	}
	if (source.contains("overrides"))
	{
		if (!source.at("overrides").is_array())
			fail("overrides must be ordered array");
		for (const auto &override : source.at("overrides"))
		{
			const auto name = override.at("state").get<std::string>();
			bool matched = false;
			for (auto &state : result.at("states"))
				if (state.at("id") == name)
				{
					for (auto it = override.begin(); it != override.end(); ++it)
						if (it.key() != "state")
							state[it.key()] = it.value();
					matched = true;
				}
			if (!matched)
				fail("unresolved structural override '" + name + "'");
		}
	}
	return result;
}
struct Compiler
{
	const Registry &registry;
	const Catalog &catalog;
	std::vector<std::string> calls;
	std::size_t total_nodes{};
	std::size_t total_tasks{};
	Program build(const Json &input, const Json &supplied)
	{
		std::vector<std::string> stack;
		const Json source = resolve(input, catalog, stack);
		Program program;
		const auto context = source.at("context").get<std::string>();
		if (context != "system" && context != "scenario")
			fail("context must be system or scenario");
		program.context = context == "system" ? Context::System : Context::Scenario;
		program.registry = registry;
		program.budget = source.value("budget", 4096u);
		if (program.budget < 1 || program.budget > 65536)
			fail("budget outside 1..65536");
		std::map<std::string, Value> parameters;
		Json arguments = source.value("arguments", Json::object());
		arguments.update(supplied);
		const auto declared = source.value("parameters", Json::object());
		for (auto it = declared.begin(); it != declared.end(); ++it)
		{
			const auto type = parse_type(it.value().at("type").get<std::string>());
			const auto value =
			    value_from(arguments.contains(it.key()) ? arguments.at(it.key()) : it.value().at("default"));
			if (!valid_value(value, type))
				fail("parameter type mismatch: " + it.key());
			parameters.emplace(it.key(), value);
		}
		for (auto it = arguments.begin(); it != arguments.end(); ++it)
			if (!parameters.contains(it.key()))
				fail("undeclared parameter: " + it.key());
		program.parameters = parameters;
		if (source.contains("memory"))
		{
			if (!source.at("memory").is_array() || source.at("memory").size() > 1024)
				fail("memory must be bounded array");
			std::set<std::string> names;
			for (const auto &field : source.at("memory"))
			{
				FieldSpec spec{
				    field.at("name").get<std::string>(), parse_type(field.at("type").get<std::string>()), 3, {}};
				if (!valid_identifier(spec.name) || !names.insert(spec.name).second)
					fail("duplicate/invalid memory name");
				const auto &initial = field.at("initial");
				if (initial.is_object() && initial.contains("parameter"))
					spec.initial = parameters.at(initial.at("parameter").get<std::string>());
				else
					spec.initial = value_from(initial);
				if (!valid_value(spec.initial, spec.type))
					fail("invalid memory initial value: " + spec.name);
				program.memory.push_back(std::move(spec));
			}
		}
		const auto binding = [&](const Json &json) -> Binding
		{
			Binding result;
			if (json.is_object() && json.contains("parameter"))
			{
				const auto name = json.at("parameter").get<std::string>();
				if (!parameters.contains(name))
					fail("unresolved parameter '" + name + "'");
				result.literal = parameters.at(name);
			}
			else if (json.is_object() && json.contains("elapsed"))
			{
				if (json.at("elapsed") != true)
					fail("elapsed binding must be true");
				result.source = Source::Elapsed;
				result.type = Type::Integer;
				return result;
			}
			else
			{
				for (const auto &[key, source_kind] : std::vector<std::pair<std::string, Source>>{
				         {"observation", Source::Observation}, {"intent", Source::Intent}, {"memory", Source::Memory}})
					if (json.is_object() && json.contains(key))
					{
						const auto &fields = source_kind == Source::Observation ? registry.observations
						                     : source_kind == Source::Intent    ? registry.intents
						                                                        : program.memory;
						result.source = source_kind;
						result.index = field_index(fields, json.at(key).get<std::string>());
						field_context(fields[result.index], program.context);
						result.type = fields[result.index].type;
						return result;
					}
				result.literal = value_from(json);
			}
			result.type = result.literal.type;
			return result;
		};
		const auto &states = source.at("states");
		if (!states.is_array() || states.empty() || states.size() > 1024 || (total_nodes += states.size()) > 8192)
			fail("states must be ordered bounded nonempty array");
		std::map<std::string, Index> names;
		for (const auto &state : states)
		{
			const auto name = state.at("id").get<std::string>();
			if (!valid_identifier(name) || !names.emplace(name, static_cast<Index>(names.size())).second)
				fail("duplicate/invalid state '" + name + "'");
			program.states.push_back(State{name});
		}
		const auto state_index = [&](const Json &json)
		{
			const auto name = json.get<std::string>();
			if (!names.contains(name))
				fail("unresolved state '" + name + "'");
			return names.at(name);
		};
		program.initial = state_index(source.at("initial"));
		Index next_task = 0;
		const auto tasks = [&](const Json &list) -> std::vector<Task>
		{
			if (!list.is_array() || list.size() > 1024)
				fail("tasks must be ordered bounded array");
			std::vector<Task> result;
			for (const auto &json : list)
			{
				if (next_task >= 8192 || ++total_tasks > 65536)
					fail("task count exceeded");
				Task task;
				task.id = next_task++;
				const auto alternatives =
				    static_cast<int>(json.contains("memory")) + static_cast<int>(json.contains("intent")) +
				    static_cast<int>(json.contains("action")) + static_cast<int>(json.contains("call"));
				if (alternatives != 1)
					fail("task requires exactly one operation");
				if (json.contains("memory") || json.contains("intent"))
				{
					const bool memory = json.contains("memory");
					task.kind = memory ? TaskKind::Memory : TaskKind::Intent;
					const auto &fields = memory ? program.memory : registry.intents;
					task.target = field_index(fields, json.at(memory ? "memory" : "intent").get<std::string>());
					field_context(fields[task.target], program.context);
					task.arguments.push_back(binding(json.at("value")));
					if (task.arguments.front().type != fields[task.target].type)
						fail("assignment type mismatch");
				}
				else if (json.contains("action"))
				{
					task.kind = TaskKind::Action;
					const auto name = json.at("action").get<std::string>();
					for (Index i = 0; i < registry.actions.size(); ++i)
						if (registry.actions[i].name == name)
							task.target = i;
					if (task.target == none)
						fail("unavailable action '" + name + "'");
					const auto &spec = registry.actions[task.target];
					if (!(spec.contexts & static_cast<unsigned>(program.context)))
						fail("action outside permitted context '" + name + "'");
					if (spec.intent_field != none)
						field_context(registry.intents[spec.intent_field], program.context);
					task.once = spec.effect;
					const auto args = json.value("arguments", Json::array());
					if (!args.is_array() || args.size() != spec.arguments.size())
						fail("action argument count: " + name);
					for (Index i = 0; i < args.size(); ++i)
					{
						task.arguments.push_back(binding(args[i]));
						if (task.arguments.back().type != spec.arguments[i])
							fail("action argument type: " + name);
					}
				}
				else
				{
					task.kind = TaskKind::Call;
					const auto name = json.at("call").get<std::string>();
					if (!catalog.contains(name))
						fail("unresolved reference '" + name + "'");
					if (calls.size() >= 32 || std::find(calls.begin(), calls.end(), name) != calls.end())
						fail("reference recursion/depth: " + name);
					Json args = json.value("parameters", Json::object());
					for (auto it = args.begin(); it != args.end(); ++it)
						if (it.value().is_object() && it.value().contains("parameter"))
						{
							const auto &value = parameters.at(it.value().at("parameter").get<std::string>());
							if (value.type == Type::Boolean)
								it.value() = value.number != 0;
							else if (value.type == Type::Integer)
								it.value() = value.number;
							else if (value.type == Type::Text)
								it.value() = value.text;
							else
							{
								static const std::vector<std::string> types{
								    "missing", "boolean", "integer",   "text",   "entity",
								    "system",  "console", "objective", "region", "message"};
								it.value() =
								    Json{{"type", types[static_cast<unsigned>(value.type)]}, {"id", value.number}};
							}
						}
					calls.push_back(name);
					auto child = build(catalog.at(name), args);
					calls.pop_back();
					if (child.context != program.context)
						fail("reference context mismatch: " + name);
					task.target = static_cast<Index>(program.references.size());
					program.references.push_back(std::move(child));
				}
				task.once = json.value("once", task.once);
				if (task.kind == TaskKind::Action && registry.actions[task.target].effect && !task.once)
					fail("effect actions execute once per activation");
				if (task.kind == TaskKind::Call && task.once)
					fail("reference calls cannot be once; place in running tasks");
				result.push_back(std::move(task));
			}
			return result;
		};
		for (Index i = 0; i < states.size(); ++i)
		{
			const auto &json = states[i];
			auto &state = program.states[i];
			if (json.contains("parent"))
				state.parent = state_index(json.at("parent"));
			if (json.contains("initial"))
				state.initial = state_index(json.at("initial"));
			state.entry = tasks(json.value("entry", Json::array()));
			state.running = tasks(json.value("running", Json::array()));
			state.exit = tasks(json.value("exit", Json::array()));
			const auto transitions = json.value("transitions", Json::array());
			if (!transitions.is_array() || transitions.size() > 1024)
				fail("transitions must be ordered bounded array");
			for (const auto &transition : transitions)
			{
				Transition out;
				out.target = state_index(transition.at("target"));
				out.reason = transition.value("reason", state.name);
				const auto conditions = transition.value("conditions", Json::array());
				if (!conditions.is_array() || conditions.size() > 128)
					fail("conditions must be ordered bounded array");
				for (const auto &condition : conditions)
				{
					Condition test;
					test.left = binding(condition.at("left"));
					const auto op = condition.at("op").get<std::string>();
					static const std::vector<std::string> ops{"eq", "ne", "lt", "le", "gt", "ge", "present"};
					const auto found = std::find(ops.begin(), ops.end(), op);
					if (found == ops.end())
						fail("unsupported comparison");
					test.operation = static_cast<Compare>(found - ops.begin());
					if (test.operation != Compare::Present)
					{
						test.right = binding(condition.at("right"));
						if (test.left.type != test.right.type)
							fail("comparison type mismatch");
						if (test.operation > Compare::NotEqual && test.left.type != Type::Integer)
							fail("ordered comparison requires integer");
					}
					out.conditions.push_back(std::move(test));
				}
				state.transitions.push_back(std::move(out));
			}
		}
		for (Index i = 0; i < program.states.size(); ++i)
		{
			const auto &state = program.states[i];
			if (state.initial != none && program.states[state.initial].parent != i)
				fail("initial must name direct child");
			bool has_children = false;
			for (const auto &candidate : program.states)
				if (candidate.parent == i)
					has_children = true;
			if (has_children && state.initial == none)
				fail("compound state needs initial child");
			std::set<Index> ancestors;
			for (Index at = i; at != none; at = program.states[at].parent)
				if (!ancestors.insert(at).second || ancestors.size() > 64)
					fail("hierarchy cycle/depth");
		}
		if (program.states[program.initial].parent != none)
			fail("tree initial must be root state");
		Json fingerprint{{"source", source}, {"arguments", arguments}};
		fingerprint["registry"] = Json::array();
		for (const auto &f : registry.observations)
			fingerprint["registry"].push_back(
			    Json::array({"observation", f.name, static_cast<unsigned>(f.type), f.contexts}));
		for (const auto &f : registry.intents)
			fingerprint["registry"].push_back(
			    Json::array({"intent", f.name, static_cast<unsigned>(f.type), f.contexts, value_json(f.initial)}));
		for (const auto &a : registry.actions)
		{
			Json types = Json::array();
			for (auto t : a.arguments)
				types.push_back(static_cast<unsigned>(t));
			fingerprint["registry"].push_back(
			    Json::array({"action", a.name, a.contexts, types, a.effect, a.channel, a.target_argument,
			                 a.intent_field, a.value_argument, a.key_arguments, a.capture_context}));
		}
		for (const auto &ref : program.references)
			fingerprint["references"].push_back(ref.signature);
		const auto encoded = fingerprint.dump();
		program.signature = sha256(
		    std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(encoded.data()), encoded.size()));
		return program;
	}
};
void validate_registry(const Registry &registry)
{
	if (registry.observations.size() > 4096 || registry.intents.size() > 4096 || registry.actions.size() > 4096)
		fail("registry exceeds layout limit");
	for (const auto *fields : {&registry.observations, &registry.intents})
	{
		std::set<std::string> names;
		for (const auto &field : *fields)
		{
			if (!valid_identifier(field.name) || !names.insert(field.name).second || field.type <= Type::Missing ||
			    field.type > Type::Message || field.contexts == 0 || field.contexts > 3)
				fail("invalid/duplicate registry field: " + field.name);
			if (!valid_value(field.initial, field.type, true))
				fail("invalid field default: " + field.name);
		}
	}
	std::set<std::string> names;
	for (const auto &action : registry.actions)
	{
		if (!valid_identifier(action.name) || !names.insert(action.name).second || action.contexts == 0 ||
		    action.contexts > 3 || action.arguments.size() > 64)
			fail("invalid/duplicate action: " + action.name);
		for (auto type : action.arguments)
			if (type <= Type::Missing || type > Type::Message)
				fail("invalid action argument type");
		if (action.target_argument != none && action.target_argument >= action.arguments.size())
			fail("invalid action target argument");
		std::set<Index> keys;
		for (auto key : action.key_arguments)
			if (key >= action.arguments.size() || !keys.insert(key).second)
				fail("invalid/duplicate action conflict key");
		if (action.intent_field != none &&
		    (action.intent_field >= registry.intents.size() || action.value_argument >= action.arguments.size() ||
		     registry.intents[action.intent_field].type != action.arguments[action.value_argument]))
			fail("invalid action intent binding");
	}
}
} // namespace

Program compile(const Json &definition, const Registry &registry, const Catalog &catalog)
{
	validate_registry(registry);
	Compiler compiler{registry, catalog, {}, 0};
	return compiler.build(definition, Json::object());
}
} // namespace phoenix::state_tree
