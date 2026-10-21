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
namespace
{
#include "state_tree_validation.hpp"
[[noreturn]] void fail(const std::string &message)
{
	throw std::runtime_error("state tree: " + message);
}
std::uint64_t unsigned_number(const Json &json, std::uint64_t maximum, const std::string &field)
{
	return checked_unsigned(json, 0, maximum, "state tree: saved " + field);
}
Json value_json(const Value &value)
{
	return Json{{"type", static_cast<unsigned>(value.type)}, {"number", value.number}, {"text", value.text}};
}
Value saved_value(const Json &json)
{
	const auto type = unsigned_number(json.at("type"), static_cast<unsigned>(Type::Message), "value.type");
	return {static_cast<Type>(type),
	        checked_integer(json.at("number"), std::numeric_limits<std::int64_t>::min(),
	                        std::numeric_limits<std::int64_t>::max(), "state tree: saved value"),
	        json.at("text").get<std::string>()};
}
struct Evaluation
{
	std::span<const Value> observations;
	std::uint64_t ticks;
	std::uint32_t budget;
	Result result;
	std::vector<Index> call_path;
	std::vector<std::uint64_t> call_activations;
	void spend(std::size_t work = 1)
	{
		if (work > budget - result.debug.work)
			fail("evaluation budget exhausted; no continuation or effects committed");
		result.debug.work += static_cast<std::uint32_t>(work);
	}
	void spend_capture(const std::string &name, const Value &value)
	{
		// Charge fixed map/value work and copied name/text bytes before allocating
		// captured maps. This bounds the total retained emission context per step.
		spend(1 + (name.size() + 63) / 64 + (value.text.size() + 63) / 64);
	}
	Value read(const Binding &binding, const Instance &instance, const Frame &frame)
	{
		switch (binding.source)
		{
		case Source::Literal:
			return binding.literal;
		case Source::Observation:
			return observations[binding.index];
		case Source::Intent:
			return result.intents[binding.index];
		case Source::Memory:
			return instance.memory[binding.index];
		case Source::Elapsed:
			return Value::integer(static_cast<std::int64_t>(
			    std::min<std::uint64_t>(frame.elapsed, std::numeric_limits<std::int64_t>::max())));
		}
		return {};
	}
	bool matches(const Condition &condition, const Instance &instance, const Frame &frame)
	{
		spend();
		const auto left = read(condition.left, instance, frame);
		if (condition.operation == Compare::Present)
			return compare_values(left, condition.operation, {});
		return compare_values(left, condition.operation, read(condition.right, instance, frame));
	}
	void emit(const Program &program, Output output)
	{
		const auto &spec = program.registry.actions[output.action];
		if (!spec.effect)
		{
			const auto channel = spec.channel == none ? output.action : spec.channel;
			std::erase_if(result.outputs,
			              [&](const Output &prior)
			              {
				              spend();
				              const auto &other = program.registry.actions[prior.action];
				              const auto other_channel = other.channel == none ? prior.action : other.channel;
				              if (other.effect || channel != other_channel)
					              return false;
				              const auto key_count = [](const ActionSpec &action)
				              {
					              return !action.key_arguments.empty()    ? action.key_arguments.size()
					                     : action.target_argument == none ? std::size_t{0}
					                                                      : std::size_t{1};
				              };
				              const auto key_at = [](const ActionSpec &action, std::size_t index)
				              {
					              return !action.key_arguments.empty() ? action.key_arguments[index]
					                                                   : action.target_argument;
				              };
				              if (key_count(spec) != key_count(other))
					              return false;
				              for (std::size_t index = 0; index < key_count(spec); ++index)
					              if (prior.arguments[key_at(other, index)] != output.arguments[key_at(spec, index)])
						              return false;
				              return true;
			              });
		}
		result.outputs.push_back(std::move(output));
	}
	void tasks(const Program &program, Instance &instance, Frame &frame, const std::vector<Task> &tasks)
	{
		for (const auto &task : tasks)
		{
			spend();
			if (task.once && std::find(frame.fired.begin(), frame.fired.end(), task.id) != frame.fired.end())
				continue;
			if (task.kind == TaskKind::Call)
			{
				call_path.push_back(task.id);
				call_activations.push_back(frame.activation);
				run(program.references[task.target], instance.calls[task.id]);
				call_activations.pop_back();
				call_path.pop_back();
			}
			else
			{
				std::vector<Value> args;
				bool complete = true;
				for (const auto &binding : task.arguments)
				{
					args.push_back(read(binding, instance, frame));
					if (args.back().type == Type::Missing)
						complete = false;
				}
				if (!complete)
				{
					result.debug.diagnostics.push_back("task " + std::to_string(task.id) +
					                                   ": unavailable permitted input");
					continue;
				}
				if (task.kind == TaskKind::Memory)
					instance.memory[task.target] = args.front();
				else if (task.kind == TaskKind::Intent)
					result.intents[task.target] = args.front();
				else
				{
					const auto &spec = program.registry.actions[task.target];
					if (spec.intent_field != none)
						result.intents[spec.intent_field] = args[spec.value_argument];
					Output output{task.target, std::move(args), frame.activation, task.id, call_path, call_activations};
					if (spec.capture_context)
					{
						for (const auto &[name, value] : program.parameters)
							spend_capture(name, value);
						for (std::size_t i = 0; i < program.memory.size(); ++i)
							spend_capture(program.memory[i].name, instance.memory.at(i));
						for (std::size_t i = 0; i < program.registry.intents.size(); ++i)
							spend_capture(program.registry.intents[i].name, result.intents.at(i));
						output.captured_parameters = program.parameters;
						output.captured_elapsed = frame.elapsed;
						for (std::size_t i = 0; i < program.memory.size(); ++i)
							output.captured_memory.emplace(program.memory[i].name, instance.memory.at(i));
						for (std::size_t i = 0; i < program.registry.intents.size(); ++i)
							output.captured_intents.emplace(program.registry.intents[i].name, result.intents.at(i));
					}
					emit(program, std::move(output));
				}
			}
			if (task.once)
				frame.fired.push_back(task.id);
		}
	}
	void enter(const Program &program, Instance &instance, Index state)
	{
		while (state != none)
		{
			spend();
			if (instance.next_activation == std::numeric_limits<std::uint64_t>::max())
				fail("activation sequence exhausted");
			instance.path.push_back(Frame{state, 0, instance.next_activation++, {}});
			tasks(program, instance, instance.path.back(), program.states[state].entry);
			state = program.states[state].initial;
		}
	}
	void exit_last(const Program &program, Instance &instance)
	{
		auto &frame = instance.path.back();
		const auto &state = program.states[frame.state];
		// Child decision work is interrupted before its caller exits.
		for (const auto *list : {&state.entry, &state.running, &state.exit})
			for (const auto &task : *list)
				if (task.kind == TaskKind::Call && instance.calls.contains(task.id))
				{
					call_path.push_back(task.id);
					call_activations.push_back(frame.activation);
					stop(program.references[task.target], instance.calls.at(task.id));
					call_activations.pop_back();
					call_path.pop_back();
					instance.calls.erase(task.id);
				}
		tasks(program, instance, frame, state.exit);
		// Exit tasks may have started reference work; stop it as well.
		for (const auto &task : state.exit)
			if (task.kind == TaskKind::Call && instance.calls.contains(task.id))
			{
				call_path.push_back(task.id);
				call_activations.push_back(frame.activation);
				stop(program.references[task.target], instance.calls.at(task.id));
				call_activations.pop_back();
				call_path.pop_back();
				instance.calls.erase(task.id);
			}
		instance.path.pop_back();
	}
	void stop(const Program &program, Instance &instance)
	{
		if (instance.interrupted)
			return;
		while (!instance.path.empty())
			exit_last(program, instance);
		instance.calls.clear();
		instance.interrupted = true;
		instance.resume_pending = false;
	}
	void run(const Program &program, Instance &instance)
	{
		if (instance.interrupted)
			return;
		if (!instance.initialised || instance.resume_pending)
		{
			instance.resume_pending = false;
			instance.initialised = true;
			if (instance.memory.empty())
				for (const auto &field : program.memory)
					instance.memory.push_back(field.initial);
			enter(program, instance, program.initial);
		}
		for (auto &frame : instance.path)
		{
			tasks(program, instance, frame, program.states[frame.state].running);
			if (ticks > std::numeric_limits<std::uint64_t>::max() - frame.elapsed)
				fail("elapsed timer exhausted");
			frame.elapsed += ticks;
		}
		// Deepest active state has priority; arrays establish order within a state.
		for (std::size_t depth = instance.path.size(); depth > 0; --depth)
		{
			const auto &frame = instance.path[depth - 1];
			const auto &state = program.states[frame.state];
			for (const auto &transition : state.transitions)
			{
				spend();
				bool matched = true;
				for (const auto &condition : transition.conditions)
					if (!matches(condition, instance, frame))
					{
						matched = false;
						break;
					}
				if (!matched)
					continue;
				std::vector<Index> target_path;
				for (Index at = transition.target; at != none; at = program.states[at].parent)
					target_path.push_back(at);
				std::reverse(target_path.begin(), target_path.end());
				std::size_t common = 0;
				while (common < instance.path.size() && common < target_path.size() &&
				       instance.path[common].state == target_path[common])
					++common;
				// External transitions re-enter their source when targeting self/descendants.
				common = std::min(common, depth - 1);
				const auto reason = transition.reason;
				while (instance.path.size() > common)
					exit_last(program, instance);
				for (std::size_t at = common; at < target_path.size(); ++at)
				{
					spend();
					if (instance.next_activation == std::numeric_limits<std::uint64_t>::max())
						fail("activation sequence exhausted");
					instance.path.push_back(Frame{target_path[at], 0, instance.next_activation++, {}});
					tasks(program, instance, instance.path.back(), program.states[target_path[at]].entry);
				}
				const auto child = program.states[instance.path.back().state].initial;
				if (child != none)
					enter(program, instance, child);
				result.debug.transition = reason;
				return;
			}
		}
	}
};

Evaluation evaluation(const Program &program, std::span<const Value> observations, std::uint64_t ticks)
{
	if (observations.size() != program.registry.observations.size())
		fail("observation layout mismatch");
	for (Index i = 0; i < observations.size(); ++i)
	{
		if (!valid_value(observations[i], program.registry.observations[i].type, true))
			fail("observation type mismatch");
		if (!(program.registry.observations[i].contexts & static_cast<unsigned>(program.context)) &&
		    observations[i].type != Type::Missing)
			fail("observation outside context");
	}
	Evaluation result{observations, ticks, program.budget, {}, {}, {}};
	for (const auto &field : program.registry.intents)
		result.result.intents.push_back(field.initial);
	return result;
}
const Task *find_call(const Program &program, Index id)
{
	for (const auto &state : program.states)
		for (const auto *tasks : {&state.entry, &state.running, &state.exit})
			for (const auto &task : *tasks)
				if (task.id == id && task.kind == TaskKind::Call)
					return &task;
	return nullptr;
}
Json save_instance(const Program &program, const Instance &instance)
{
	Json result{{"initialised", instance.initialised},
	            {"interrupted", instance.interrupted},
	            {"resume_pending", instance.resume_pending},
	            {"next_activation", instance.next_activation},
	            {"path", Json::array()},
	            {"memory", Json::array()},
	            {"calls", Json::array()}};
	for (const auto &frame : instance.path)
		result["path"].push_back(Json{{"state", frame.state},
		                              {"elapsed", frame.elapsed},
		                              {"activation", frame.activation},
		                              {"fired", frame.fired}});
	for (const auto &value : instance.memory)
		result["memory"].push_back(value_json(value));
	for (const auto &[id, child] : instance.calls)
	{
		const auto task = find_call(program, id);
		if (!task)
			fail("invalid call continuation");
		result["calls"].push_back(
		    Json{{"task", id}, {"instance", save_instance(program.references[task->target], child)}});
	}
	return result;
}
Instance load_instance(const Program &program, const Json &json)
{
	Instance result;
	result.initialised = json.at("initialised").get<bool>();
	result.interrupted = json.at("interrupted").get<bool>();
	result.resume_pending = json.value("resume_pending", false);
	result.next_activation =
	    unsigned_number(json.at("next_activation"), std::numeric_limits<std::uint64_t>::max(), "next_activation");
	if (!result.next_activation || !json.at("path").is_array() || json.at("path").size() > 64 ||
	    !json.at("memory").is_array() || !json.at("calls").is_array() || json.at("calls").size() > 8192)
		fail("invalid saved instance shape");
	Index parent = none;
	std::set<std::uint64_t> activations;
	for (const auto &saved : json.at("path"))
	{
		Frame frame{static_cast<Index>(unsigned_number(saved.at("state"), std::numeric_limits<Index>::max(), "state")),
		            unsigned_number(saved.at("elapsed"), std::numeric_limits<std::uint64_t>::max(), "elapsed"),
		            unsigned_number(saved.at("activation"), std::numeric_limits<std::uint64_t>::max(), "activation"),
		            {}};
		if (!saved.at("fired").is_array() || saved.at("fired").size() > 8192)
			fail("invalid saved one-shot list");
		for (const auto &id : saved.at("fired"))
			frame.fired.push_back(static_cast<Index>(unsigned_number(id, std::numeric_limits<Index>::max(), "fired")));
		if (frame.state >= program.states.size() || program.states[frame.state].parent != parent || !frame.activation ||
		    frame.activation >= result.next_activation || !activations.insert(frame.activation).second ||
		    frame.fired.size() > 8192)
			fail("invalid saved active path");
		std::set<Index> fired;
		for (auto id : frame.fired)
		{
			bool found = false;
			for (const auto *tasks : {&program.states[frame.state].entry, &program.states[frame.state].running,
			                          &program.states[frame.state].exit})
				for (const auto &task : *tasks)
					if (task.id == id && task.once)
						found = true;
			if (!found || !fired.insert(id).second)
				fail("invalid saved one-shot marker");
		}
		parent = frame.state;
		result.path.push_back(std::move(frame));
	}
	if (!result.path.empty() && program.states[result.path.back().state].initial != none)
		fail("saved path must end at leaf");
	for (const auto &value : json.at("memory"))
		result.memory.push_back(saved_value(value));
	if (result.memory.size() != (result.initialised ? program.memory.size() : 0))
		fail("saved memory layout mismatch");
	for (Index i = 0; i < result.memory.size(); ++i)
		if (!valid_value(result.memory[i], program.memory[i].type))
			fail("saved memory type mismatch");
	if ((!result.initialised && !result.path.empty()) || (result.interrupted && !result.path.empty()) ||
	    (result.initialised && !result.interrupted && !result.resume_pending && result.path.empty()) ||
	    (result.resume_pending && (!result.initialised || result.interrupted || !result.path.empty())))
		fail("invalid saved lifecycle");
	for (const auto &saved : json.at("calls"))
	{
		const auto id =
		    static_cast<Index>(unsigned_number(saved.at("task"), std::numeric_limits<Index>::max(), "task"));
		const auto task = find_call(program, id);
		if (!task || result.calls.contains(id))
			fail("invalid saved reference call");
		bool active = false;
		for (const auto &frame : result.path)
			for (const auto *tasks : {&program.states[frame.state].entry, &program.states[frame.state].running})
				for (const auto &candidate : *tasks)
					if (candidate.id == id)
						active = true;
		if (!active)
			fail("reference call outside active state");
		result.calls.emplace(id, load_instance(program.references[task->target], saved.at("instance")));
	}
	return result;
}
} // namespace

Result step(const Program &program, Instance &instance, std::span<const Value> observations, std::uint64_t ticks)
{
	if (ticks == 0 || ticks > 1000000)
		fail("ticks outside 1..1000000");
	auto eval = evaluation(program, observations, ticks);
	// Work is transactional: resource/action admission happens only after a complete
	// successful evaluation. Budget failure cannot advance one-shot continuation.
	auto next = instance;
	eval.run(program, next);
	for (const auto &frame : next.path)
		eval.result.debug.active.push_back(program.states[frame.state].name);
	instance = std::move(next);
	return std::move(eval.result);
}
Result interrupt(const Program &program, Instance &instance, std::span<const Value> observations)
{
	auto eval = evaluation(program, observations, 0);
	auto next = instance;
	eval.stop(program, next);
	instance = std::move(next);
	return std::move(eval.result);
}
void reset(Instance &instance)
{
	const auto next_activation = instance.next_activation;
	instance = {};
	instance.next_activation = next_activation;
}
void resume(Instance &instance)
{
	if (instance.interrupted)
	{
		instance.interrupted = false;
		instance.resume_pending = instance.initialised;
		instance.path.clear();
		instance.calls.clear();
	}
}
Json save(const Program &program, const Instance &instance)
{
	return Json{{"version", 1}, {"program", program.signature}, {"instance", save_instance(program, instance)}};
}
Instance load(const Program &program, const Json &continuation)
{
	if (continuation.at("version") != 1 || continuation.at("program") != program.signature)
		fail("incompatible saved tree program");
	return load_instance(program, continuation.at("instance"));
}
} // namespace phoenix::state_tree
