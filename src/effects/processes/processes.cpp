module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

module phoenix.effects.processes;

namespace phoenix
{
RuleActor process_actor(const CustomPropertyAddress &address)
{
	return {address.entity,
	        address.equipment,
	        address.officer,
	        false,
	        address.compartment >= 0 ? PayloadOutcomeKind::Compartment : PayloadOutcomeKind::Entity,
	        address.compartment};
}

std::string process_owner_kind(const CustomPropertyAddress &address)
{
	return !address.equipment.empty() ? "equipment"
	       : !address.officer.empty() ? "officer"
	       : address.compartment >= 0 ? "compartment"
	                                  : "entity";
}

std::string process_owner_name(const CustomPropertyAddress &address)
{
	return !address.equipment.empty() ? address.equipment
	       : !address.officer.empty() ? address.officer
	       : address.compartment >= 0 ? std::to_string(address.compartment)
	                                  : std::to_string(address.entity);
}

std::string process_instance_key(const CustomPropertyAddress &address, const ProcessState &state)
{
	const auto value = std::to_string(address.entity) + ":" + process_owner_kind(address) + ":" +
	                   process_owner_name(address) + ":" + state.id + ":" + std::to_string(state.generation);
	return "process." +
	       sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(value.data()), value.size()));
}

std::string process_run_source(const CustomPropertyAddress &address, const ProcessState &state)
{
	return process_instance_key(address, state) + ".run." + std::to_string(state.run);
}

void remove_process_grant(ProcessRuntime &runtime, const std::string &source)
{
	std::erase_if(runtime.processes,
	              [&](const auto &row)
	              {
		              return !row.second.source.empty() && row.second.source == source;
	              });
}

ProcessRuntime initial_process_runtime(std::span<const ProcessBinding> bindings, const ProcessCatalog &catalog)
{
	ProcessRuntime runtime;
	for (const auto &binding : bindings)
	{
		const auto &definition = catalog.at(binding.process);
		ProcessState state;
		state.id = state.slot = binding.id;
		state.definition = binding.process;
		state.native_id = binding.native_id;
		state.required_work = definition.required_work;
		state.requested = binding.requested;
		state.phase = state.requested ? "paused" : "off";
		state.generation = runtime.next_generation++;
		runtime.processes.emplace(state.id, std::move(state));
	}
	return runtime;
}

std::string install_process_grant(ProcessRuntime &runtime, const ProcessCatalog &catalog,
                                  const ProcessGrantDefinition &grant, const std::string &effect,
                                  const std::string &source, const std::string &slot, std::int64_t tick,
                                  const NativeRuleContext &origin)
{
	validate_effect_identity(source, {}, false);
	const auto id = "temporary:" + source;
	const auto expiry = effect_expiry(tick, grant.duration_ticks);
	if (auto at = runtime.processes.find(id); at != runtime.processes.end())
	{
		if (at->second.definition != grant.process || at->second.grant_effect != effect || at->second.slot != slot ||
		    at->second.native_id != grant.native_id)
			throw std::runtime_error("Process grant identity belongs to another definition");
		at->second.expiry = refresh_effect_expiry(at->second.expiry, expiry);
		for (auto &[child_id, child] : runtime.processes)
			if (child.parent_process == id)
				child.expiry = at->second.expiry;
		return id;
	}
	if (runtime.processes.size() >= process_owner_limit || !runtime.next_generation ||
	    runtime.next_generation >= process_generation_limit)
		throw std::runtime_error("Process grant capacity exhausted");
	ProcessState state;
	state.id = id;
	state.slot = slot;
	state.definition = grant.process;
	state.source = source;
	state.grant_effect = effect;
	state.native_id = grant.native_id;
	state.generation = runtime.next_generation++;
	state.expiry = expiry;
	state.origin = origin;
	state.requested = catalog.at(grant.process).requested;
	state.required_work = catalog.at(grant.process).required_work;
	state.phase = state.requested ? "paused" : "off";
	runtime.processes.emplace(id, std::move(state));
	return id;
}

void reset_process_run_state(ProcessState &state, const ProcessDefinition &definition, bool clear_request)
{
	state.admitted = false;
	if (clear_request)
		state.requested = false;
	state.phase = state.requested ? "paused" : "off";
	state.idle_output = state.work_output = 0;
	state.idle_credits.clear();
	state.work_credits.clear();
	state.work_remainder = 0;
	state.completed = state.ready = false;
	if (definition.native == NativeProcessKind::Effects)
		state.progress = 0;
}

std::string stage_process_funding_state(ProcessState &state, const ProcessDefinition &definition, std::int64_t tick,
                                        std::int64_t idle, std::int64_t activity)
{
	if (idle < 0 || idle > 1000 || activity < 0 || activity > 1000)
		throw std::runtime_error("Invalid process funding activity");
	if (!state.admitted || !state.requested)
		return {};
	if (idle <= 0 || (definition.shortage != "proportional" && (idle < 1000 || activity < 1000)))
	{
		state.idle_output = state.work_output = 0;
		state.blocker = "Insufficient process funding";
		if (definition.shortage == "stop")
		{
			reset_process_run_state(state, definition, true);
			return "stop";
		}
		const bool pausing = state.phase == "running";
		state.phase = "paused";
		return pausing ? "pause" : std::string{};
	}
	const bool resuming = state.phase == "paused" && state.started < tick;
	state.idle_output = idle;
	state.work_output = activity;
	state.phase = "running";
	state.blocker.clear();
	return resuming ? "resume" : std::string{};
}

std::optional<EffectGroupDefinition> process_funding_effect(const ProcessDefinition &definition,
                                                            const ProcessState &state, std::int64_t tick,
                                                            std::int64_t idle, std::int64_t activity)
{
	auto candidate = state;
	const auto transition = stage_process_funding_state(candidate, definition, tick, idle, activity);
	return transition == "pause"    ? definition.pause_effect
	       : transition == "resume" ? definition.resume_effect
	       : transition == "stop"   ? definition.stop_effect
	                                : std::optional<EffectGroupDefinition>{};
}

std::int64_t process_effect_work(const ProcessDefinition &definition, const ProcessState &state, std::int64_t work)
{
	const auto divisor = definition.work_divisor;
	if (work < 0 || work > resource_limit || state.work_remainder < 0 || state.work_remainder >= divisor)
		throw std::runtime_error("Invalid process fractional work");
	const auto total = work + state.work_remainder;
	const auto required = definition.required_work        ? definition.required_work
	                      : !state.parent_process.empty() ? state.required_work
	                                                      : 0;
	// Native targets own their progress across contributors. A contributor may
	// finish the remaining fraction even when its own work is below one unit.
	return required && state.progress + work >= required ? total : total / divisor * divisor;
}

std::int64_t process_next_work_remainder(const ProcessDefinition &definition, const ProcessState &state,
                                         std::int64_t work)
{
	return work + state.work_remainder - process_effect_work(definition, state, work);
}

RuleWorkPayment quote_rule_work(std::span<const RuleWorkInput> inputs, bool proportional, std::int64_t maximum_output)
{
	if (inputs.size() > 4096 || maximum_output < 0 || maximum_output > 1000)
		throw std::runtime_error("rules: work bundle exceeds supported input count");
	const auto add = [](std::int64_t a, std::int64_t b)
	{
		if (a < 0 || b < 0 || a > std::numeric_limits<std::int64_t>::max() - b)
			throw std::runtime_error("rules: prepaid work overflow");
		return a + b;
	};
	const auto multiply = [](std::int64_t a, std::int64_t b)
	{
		if (a < 0 || b < 0 || (b && a > std::numeric_limits<std::int64_t>::max() / b))
			throw std::runtime_error("rules: prepaid work overflow");
		return a * b;
	};
	RuleWorkPayment payment;
	payment.output = maximum_output;
	payment.debits.resize(inputs.size());
	payment.credits.reserve(inputs.size());
	for (const auto &input : inputs)
	{
		if (input.rate < 0 || input.rate > std::numeric_limits<std::int64_t>::max() / 1000 || input.available < 0 ||
		    input.available > resource_limit || input.credit < 0 || input.purchased < 0 ||
		    (!input.electrical && input.purchased) || input.unit_work < 1 || input.unit_work > 1000000000)
			throw std::runtime_error("rules: invalid prepaid work input");
		if (input.credit > std::numeric_limits<std::int64_t>::max() - input.unit_work)
			throw std::runtime_error("rules: prepaid work overflow");
		if (input.rate)
		{
			if (input.blocked)
				payment.output = 0;
			else
			{
				const auto prepaid = add(input.credit, input.electrical ? input.purchased : 0);
				const auto required = multiply(input.rate, payment.output);
				if (prepaid < required)
				{
					const auto outstanding = required - prepaid;
					const auto units = outstanding / input.unit_work + (outstanding % input.unit_work != 0);
					// Large unspent stores need no multiplication. If this comparison
					// fails, available * denominator is strictly below required work.
					if (input.electrical || input.available < units)
					{
						const auto work =
						    add(prepaid, input.electrical ? 0 : multiply(input.available, input.unit_work));
						payment.output = std::min(payment.output, work / input.rate);
					}
				}
			}
		}
		payment.credits.push_back(input.credit);
	}
	payment.shortage = payment.output < 1000;
	if (payment.shortage && !proportional)
	{
		payment.output = 0;
		return payment;
	}
	for (std::size_t i = 0; i < inputs.size(); ++i)
	{
		const auto &input = inputs[i];
		const auto required = multiply(input.rate, payment.output);
		if (input.electrical)
			payment.credits[i] = add(input.credit, input.purchased) - required;
		else
		{
			const auto outstanding = std::max(std::int64_t{0}, required - input.credit);
			// Division/remainder avoids overflowing a ceil-division numerator.
			payment.debits[i] = outstanding / input.unit_work + (outstanding % input.unit_work != 0);
			if (payment.debits[i] > input.available)
				throw std::logic_error("rules: prepaid work quote exceeds available stock");
			payment.credits[i] = add(input.credit, multiply(payment.debits[i], input.unit_work)) - required;
		}
	}
	return payment;
}

void record_process_completion(ProcessState &state, const ProcessDefinition &definition, const std::string &target)
{
	state.target = target;
	state.completed = true;
	state.ready = true;
	if (definition.native != NativeProcessKind::Ftl)
	{
		state.admitted = false;
		state.phase = "completed";
		state.idle_output = state.work_output = 0;
		state.idle_credits.clear();
		state.work_credits.clear();
		state.work_remainder = 0;
	}
}

bool record_process_work(ProcessRuntime &runtime, const CustomPropertyAddress &address,
                         const ProcessDefinition &definition, const ProcessState &frozen, std::int64_t tick,
                         std::int64_t work, std::int64_t activity)
{
	if (!frozen.admitted || !frozen.run || work < 0 || work > resource_limit || activity < 0 || activity > 1000)
		throw std::runtime_error("Invalid completed process work");
	const auto found = runtime.processes.find(frozen.id);
	auto *state = found == runtime.processes.end() ? nullptr : &found->second;
	const bool same = state && state->generation == frozen.generation && state->run == frozen.run;
	if ((same && state->last_work == tick) || std::any_of(runtime.work.begin(), runtime.work.end(),
	                                                      [&](const auto &receipt)
	                                                      {
		                                                      return receipt.generation == frozen.generation &&
		                                                             receipt.activation == frozen.run;
	                                                      }))
		throw std::runtime_error("Process work already committed this tick");
	if (same)
	{
		state->last_work = tick;
		state->actual_work = work;
		if (definition.required_work)
			state->progress = std::min(definition.required_work, state->progress + work);
		if (state->admitted && state->phase == "running")
			state->work_output = activity;
	}
	if (!work || !activity)
		return false;
	runtime.work.push_back({process_owner_kind(address), process_owner_name(address), definition.family, definition.id,
	                        frozen.run, activity, frozen.id, work, definition.work_unit, frozen.generation});
	return same && definition.required_work && state->progress >= definition.required_work;
}
Json process_row_decision_facts(const Json &row)
{
	Json result{{"phase", row.at("phase")},
	            {"requested", row.at("requested")},
	            {"running", row.at("phase") == "running"},
	            {"paused", row.at("phase") == "paused"},
	            {"blocked", !row.at("blocker").get<std::string>().empty()},
	            {"activity", row.at("output")},
	            {"work", row.at("work")},
	            {"finite", row.at("finite")},
	            {"driver", row.at("driver")},
	            {"properties", row.at("properties")}};
	if (row.at("finite").get<bool>() && !row.at("details_available").get<bool>())
	{
		result.erase("activity");
		result.erase("work");
	}
	if (row.at("details_available").get<bool>())
		for (const auto *field : {"target", "progress", "required_work", "completed", "ready", "completion_delivered"})
			result[field] = row.at(field);
	return result;
}
bool process_has_finite_work(const ProcessDefinition &spec)
{
	return spec.required_work > 0 || spec.native == NativeProcessKind::Repair ||
	       spec.native == NativeProcessKind::Medical || spec.native == NativeProcessKind::Recovery ||
	       spec.native == NativeProcessKind::ServicingEnergy || spec.native == NativeProcessKind::ServicingAmmunition ||
	       spec.native == NativeProcessKind::Ftl;
}

bool process_is_native_driver(const ProcessDefinition &spec, const ProcessState &state)
{
	return state.parent_process.empty() &&
	       (spec.native == NativeProcessKind::Repair || spec.native == NativeProcessKind::Medical ||
	        spec.native == NativeProcessKind::Recovery || spec.native == NativeProcessKind::ServicingEnergy ||
	        spec.native == NativeProcessKind::ServicingAmmunition);
}

Json process_decision_facts(const Observation &permitted)
{
	Json result = Json::object();
	if (const auto rows = permitted.gameplay.find("processes"); rows != permitted.gameplay.end())
		for (const auto &row : *rows)
		{
			const auto endpoint = row.at("control").get<std::string>();
			if (endpoint.empty())
				continue;
			result[endpoint] = process_row_decision_facts(row);
		}
	return result;
}

bool process_job_details_visible(const ConsoleAddress &address, const CustomPropertyAddress &owner,
                                 const ProcessState &state, const ProcessDefinition &spec, const Observation *permitted)
{
	if (spec.native == NativeProcessKind::Effects || state.target.empty())
		return true;
	if (!permitted)
		return false;
	const auto &gameplay = permitted->gameplay;
	if (spec.native == NativeProcessKind::Ftl)
	{
		for (const auto &drive : gameplay.value("ftl", Json::array()))
			if (drive.at("system") == owner.equipment)
				return true;
		return false;
	}
	if (spec.native == NativeProcessKind::ServicingEnergy || spec.native == NativeProcessKind::ServicingAmmunition)
	{
		for (const auto &craft : gameplay.value("craft", Json::array()))
			if (craft.at("id") == std::to_string(state.origin.target.entity) && craft.at("bay") == owner.equipment &&
			    craft.at("phase") == "docked")
				return true;
		return false;
	}
	if (state.origin.target.entity != address.ship)
		return false;
	if (spec.native == NativeProcessKind::Repair)
	{
		for (const auto &team : gameplay.value("teams", Json::array()))
			if (team.at("id") == owner.equipment && team.value("repair", false))
				return true;
		return false;
	}
	if (spec.native == NativeProcessKind::Medical || spec.native == NativeProcessKind::Recovery)
	{
		const auto &target = state.origin.target;
		const auto &patient = target.officer.empty() ? target.equipment : target.officer;
		if (patient.empty())
			return false;
		for (const auto &team : gameplay.value("teams", Json::array()))
			for (const auto &row : team.value("patients", Json::array()))
				if (row.at("id") == patient || row.at("id") == "team:" + patient)
					return true;
		if (const auto personnel = gameplay.find("personnel"); personnel != gameplay.end())
			for (const auto &row : personnel->value("officers", Json::array()))
				if (row.at("id") == patient)
					return true;
		return false;
	}
	return false;
}

namespace process_state_fields
{
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ProcessState, id, slot, definition, source, grant_effect, native_id, generation, run,
                                   expiry, started, last_work, requested, admitted, origin, phase, idle_credits,
                                   work_credits, work_remainder, idle_output, work_output, actual_work, rate_permille,
                                   blocker, target, parent_process, progress, required_work, completed, ready)
} // namespace process_state_fields

void to_json(Json &out, const ProcessRuntime &value)
{
	out["processes"] = value.processes;
	out["next_generation"] = value.next_generation;
	out["next_run"] = value.next_run;
	out["work_tick"] = value.work_tick;
	out["work"] = value.work;
}
void from_json(const Json &in, ProcessRuntime &value)
{
	in.at("processes").get_to(value.processes);
	in.at("next_generation").get_to(value.next_generation);
	in.at("next_run").get_to(value.next_run);
	in.at("work_tick").get_to(value.work_tick);
	in.at("work").get_to(value.work);
}

void to_json(Json &value, const ProcessState &state)
{
	process_state_fields::to_json(value, state);
	value["completion_delivered"] = state.completed;
}

void from_json(const Json &value, ProcessState &state)
{
	process_state_fields::from_json(value, state);
	const auto &alias = value.at("completion_delivered");
	if (!alias.is_boolean() || alias.get<bool>() != state.completed)
		throw std::runtime_error("Invalid process completion alias");
}
std::string process_funding_key(const CustomPropertyAddress &address, const ProcessState &state, bool work)
{
	return process_instance_key(address, state) + (work ? ":work" : ":idle");
}

} // namespace phoenix
