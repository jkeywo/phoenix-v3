module;
#include <flecs.h>
#include <functional>
#include <chrono>
#include <charconv>
#include <iterator>
#include <type_traits>
#include <string_view>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <array>
#include <variant>
#include <set>
#include <stdexcept>
#include <cstdint>
#include <map>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <tuple>
#include <vector>

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::process_completed_work(const RuleActor &actor, const std::string &family) const
    -> std::optional<std::int64_t>
{
	if (!entities.contains(actor.entity))
		return {};
	std::int64_t output = 0;
	const auto tick = static_cast<std::int64_t>(ecs.get<Clock>().tick);
	for (const auto &address : custom_property_addresses(actor.entity))
	{
		if ((!actor.equipment.empty() && address.equipment != actor.equipment) ||
		    (!actor.officer.empty() && address.officer != actor.officer) ||
		    (actor.compartment >= 0 && address.compartment != actor.compartment))
			continue;
		const auto *runtime = process_runtime(address);
		if (!runtime || runtime->work_tick != tick)
			return {};
		for (const auto &receipt : runtime->work)
			if (family.empty() || receipt.family == family)
				output += receipt.output;
	}
	return output;
}

auto Runtime::save_processes() const -> Json
{
	Json saved = Json::array();
	for (const auto &[ship, entity] : entities)
		if (ship < field_identity_base)
			for (const auto &address : custom_property_addresses(ship))
				if (const auto *runtime = process_runtime(address);
				    runtime && (!runtime->processes.empty() || runtime->next_generation != 1 ||
				                runtime->next_run != 1 || !runtime->work.empty()))
					saved.push_back({{"entity", address.entity},
					                 {"equipment", address.equipment},
					                 {"officer", address.officer},
					                 {"compartment", address.compartment},
					                 {"state", *runtime}});
	return saved;
}

auto Runtime::validate_process_runtime(const CustomPropertyAddress &address, const ProcessRuntime &runtime) const
    -> void
{
	ProcessValidationPorts ports;
	ports.initial = [this, address]()
	{
		return initial_process_runtime(address);
	};
	ports.native_binding = [this, address](const ProcessGrantDefinition &grant)
	{
		return validate_process_grant(address, grant);
	};
	phoenix::validate_process_runtime(address, runtime, {*process_catalog, process_grant_templates}, ports,
	                                  rule_continuation_validator(), static_cast<std::int64_t>(ecs.get<Clock>().tick));
}

auto Runtime::validate_saved_process_state(const Json &state) const -> void
{
	rule_saved_fields(state, {"processes", "next_generation", "next_run", "work_tick", "work"});
	rule_saved_integer(state.at("next_generation"), 1);
	rule_saved_integer(state.at("next_run"), 1);
	rule_saved_integer(state.at("work_tick"), -1);
	if (!state.at("processes").is_object() || state.at("processes").size() > process_owner_limit ||
	    !state.at("work").is_array() || state.at("work").size() > process_owner_limit)
		throw std::runtime_error("Invalid process roster shape");
	for (const auto &[id, process] : state.at("processes").items())
	{
		rule_saved_fields(process, {"id",           "slot",           "definition",  "source",
		                            "grant_effect", "native_id",      "generation",  "run",
		                            "expiry",       "started",        "last_work",   "requested",
		                            "admitted",     "origin",         "phase",       "idle_credits",
		                            "work_credits", "work_remainder", "idle_output", "work_output",
		                            "actual_work",  "rate_permille",  "blocker",     "target",
		                            "progress",     "required_work",  "completed",   "completion_delivered",
		                            "ready",        "parent_process"});
		for (const auto name : {"id", "slot", "definition", "source", "grant_effect", "native_id", "phase", "blocker",
		                        "target", "parent_process"})
			rule_saved_text(process.at(name), 1024);
		for (const auto name : {"generation", "run", "work_remainder", "idle_output", "work_output", "actual_work",
		                        "rate_permille", "progress", "required_work"})
			rule_saved_integer(process.at(name));
		for (const auto name : {"expiry", "started", "last_work"})
			rule_saved_integer(process.at(name), -1);
		rule_saved_bool(process.at("requested"));
		rule_saved_bool(process.at("admitted"));
		for (const auto *name : {"completed", "completion_delivered", "ready"})
			rule_saved_bool(process.at(name));
		validate_saved_rule_context(process.at("origin"));
		for (const auto name : {"idle_credits", "work_credits"})
		{
			if (!process.at(name).is_array() || process.at(name).size() > 4096)
				throw std::runtime_error("Invalid process credit roster");
			for (const auto &credit : process.at(name))
				validate_saved_work_credit(credit);
		}
	}
	for (const auto &receipt : state.at("work"))
	{
		rule_saved_fields(receipt, {"source", "owner", "family", "definition", "activation", "output", "execution",
		                            "work", "work_unit", "generation"});
		for (const auto name : {"source", "owner", "family", "definition", "execution", "work_unit"})
			rule_saved_text(receipt.at(name), 1024);
		for (const auto name : {"activation", "output", "work", "generation"})
			rule_saved_integer(receipt.at(name));
	}
}

auto Runtime::restore_processes(const Json &saved, const std::set<EntityId> &departed_ships) -> void
{
	if (!saved.is_array())
		throw std::runtime_error("Invalid process owner roster");
	std::map<CustomPropertyAddress, ProcessRuntime> staged;
	for (const auto &row : saved)
	{
		if (!row.is_object() || row.size() != 5)
			throw std::runtime_error("Invalid process owner row");
		CustomPropertyAddress address{
		    static_cast<EntityId>(checked_integer(row.at("entity"), 1, 9007199254740991LL, "process.entity")),
		    row.at("equipment").get<std::string>(), row.at("officer").get<std::string>(),
		    checked_integer(row.at("compartment"), -1, 255, "process.compartment")};
		if ((!address.equipment.empty() && (!address.officer.empty() || address.compartment >= 0)) ||
		    (!address.officer.empty() && address.compartment >= 0) || !process_runtime(address) ||
		    staged.contains(address))
			throw std::runtime_error("Invalid native process owner identity");
		const auto &state = row.at("state");
		if (!state.is_object() || state.size() != 5 || !state.at("processes").is_object() ||
		    state.at("processes").size() > process_owner_limit || !state.at("work").is_array() ||
		    state.at("work").size() > process_owner_limit)
			throw std::runtime_error("Invalid saved process state shape");
		validate_saved_process_state(state);
		auto runtime = state.get<ProcessRuntime>();
		validate_process_runtime(address, runtime);
		if (departed_ships.contains(address.entity))
			for (const auto &[id, process] : runtime.processes)
				if (process.admitted || process.requested)
					throw std::runtime_error("Departed owner retained active process");
		staged.emplace(address, std::move(runtime));
	}
	for (const auto &[ship, entity] : entities)
		for (const auto &address : custom_property_addresses(ship))
		{
			if (!process_owner_bindings(address).empty() && !staged.contains(address))
				throw std::runtime_error("Missing process owner state");
			if (auto *runtime = process_runtime(address))
				*runtime = staged.contains(address) ? std::move(staged.at(address)) : initial_process_runtime(address);
		}
	process_retirement_pending = false;
}
} // namespace phoenix::world_detail
