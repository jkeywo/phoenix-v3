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

namespace phoenix
{
using namespace world_detail;
Json World::debug() const
{
	Json result{{"tick", ticks()},
	            {"active_states", Json::array()},
	            {"transition_reason", ""},
	            {"diagnostics", Json::array()},
	            {"controllers", Json::array()},
	            {"console_records", Json::array()},
	            {"ship_records", Json::array()},
	            {"pending_messages", Json::array()},
	            {"resource_grants", Json::array()},
	            {"pending_outputs", Json::array()},
	            {"truncated", 0}};
	const auto sample = [](const Json &rows)
	{
		Json out = Json::array();
		if (rows.is_array())
			for (std::size_t i = rows.size() > 8 ? rows.size() - 8 : 0; i < rows.size(); ++i)
				out.push_back(rows[i]);
		return out;
	};
	std::size_t controllers = 0, consoles = 0;
	for (const auto &[id, entity] : impl_->entities)
	{
		if (entity.has<EntityControls>())
			for (const auto &controller : entity.get<EntityControls>().controllers)
			{
				if (++controllers > 32)
				{
					result["truncated"] = result["truncated"].get<int>() + 1;
					continue;
				}
				Json row{
				    {"id", std::to_string(id) + "." + controller.definition.console + "." + controller.definition.id},
				    {"context", "controller"},
				    {"entity", id},
				    {"console", controller.definition.console},
				    {"controller", controller.definition.id},
				    {"operator", controller.human ? "human" : "ai"},
				    {"generation", controller.generation},
				    {"tree", controller.definition.tree},
				    {"active_states", Json::array()},
				    {"transition_reason", ""},
				    {"diagnostics", Json::array()},
				    {"commands", Json::array()}};
				row["action"] = save_controller_action(controller);
				row["coordination"] = save_controller_coordination(controller);
				if (controller.brain)
				{
					row["active_states"] = controller.brain->debug.active;
					row["transition_reason"] = controller.brain->debug.transition;
					row["diagnostics"] = controller.brain->debug.diagnostics;
					for (const auto &state : controller.brain->debug.active)
						if (result["active_states"].size() < 128)
							result["active_states"].push_back(state);
					for (const auto &diagnostic : controller.brain->debug.diagnostics)
						if (result["diagnostics"].size() < 32)
							result["diagnostics"].push_back(diagnostic);
				}
				for (std::size_t channel = 0; channel < controller.commands.size(); ++channel)
				{
					if (channel >= 64)
					{
						result["truncated"] = result["truncated"].get<int>() + 1;
						continue;
					}
					const auto &command = controller.commands[channel];
					row["commands"].push_back({{"channel", controller.definition.commands[channel].id},
					                           {"value", command.value},
					                           {"revision", command.revision},
					                           {"consumed", command.consumed},
					                           {"request_id", command.request_id},
					                           {"accepted", command.accepted}});
				}
				result["controllers"].push_back(std::move(row));
			}
		if (auto systems = gameplay_systems(entity))
			for (std::size_t i = 0; i < systems->values.size(); ++i)
			{
				const auto system = systems->values[i];
				if (systems->power)
				{
					if (result["resource_grants"].size() < 64)
					{
						const auto &grant =
						    systems->power->grants[(*fittings(entity)).values[i].entity.get<ResourceBinding>().slot];
						result["resource_grants"].push_back({{"ship", id},
						                                     {"system", (*fittings(entity)).values[i].id},
						                                     {"requested", grant.requested},
						                                     {"paid", grant.paid},
						                                     {"supply_permille", grant.supply_permille},
						                                     {"overdrive_permille", grant.overdrive_permille},
						                                     {"telemetry", impl_->telemetry(id, i)}});
					}
					else
						result["truncated"] = result["truncated"].get<int>() + 1;
				}
			}
		bool shared = false;
		for (const auto &console : impl_->definition(id).consoles)
		{
			if (++consoles > 32)
			{
				result["truncated"] = result["truncated"].get<int>() + 1;
				continue;
			}
			const auto projection = impl_->knowledge.project_json({id, console.id});
			result["console_records"].push_back({{"ship", id},
			                                     {"console", console.id},
			                                     {"records", sample(projection.value("records", Json::array()))},
			                                     {"messages", sample(projection.value("messages", Json::array()))}});
			if (!shared)
			{
				result["ship_records"].push_back(
				    {{"ship", id}, {"records", sample(projection.value("shared", Json::array()))}});
				shared = true;
			}
		}
	}
	std::size_t scenario_controllers = 0;
	for (const auto &proposal : impl_->controller_pending)
	{
		const auto &controller =
		    impl_->entities.at(proposal.address.entity).get<EntityControls>().controllers.at(proposal.controller);
		const auto &channel = controller.definition.commands.at(proposal.channel);
		if (result["pending_outputs"].size() < 32)
			result["pending_outputs"].push_back({{"ship", proposal.address.entity},
			                                     {"console", controller.definition.console},
			                                     {"controller", controller.definition.id},
			                                     {"channel", channel.id},
			                                     {"action", operating_action(channel.action).name},
			                                     {"value", proposal.value}});
		else
			result["truncated"] = result["truncated"].get<int>() + 1;
	}
	for (const auto &[claim, command] : impl_->meta_pending)
		if (result["pending_outputs"].size() < 32)
			result["pending_outputs"].push_back({{"ship", command.target},
			                                     {"system", command.system},
			                                     {"action", command.action},
			                                     {"value", command.value}});
		else
			result["truncated"] = result["truncated"].get<int>() + 1;
	if (impl_->scenario)
		for (const auto &[id, controller] : impl_->scenario->controllers())
		{
			if (++scenario_controllers > 32)
			{
				result["truncated"] = result["truncated"].get<int>() + 1;
				continue;
			}
			result["controllers"].push_back({{"id", id},
			                                 {"tree", controller.tree},
			                                 {"context", "scenario"},
			                                 {"active_states", controller.debug.active},
			                                 {"transition_reason", controller.debug.transition},
			                                 {"diagnostics", controller.debug.diagnostics}});
			for (const auto &state : controller.debug.active)
				if (result["active_states"].size() < 128)
					result["active_states"].push_back(state);
		}
	for (const auto &message : impl_->knowledge.pending_messages())
		result["pending_messages"].push_back(
		    {{"id", message.id},
		     {"sender_system", message.value.sender_system},
		     {"destination", message.value.destination.id},
		     {"due_tick", message.delivery_tick},
		     {"expires_tick", message.value.expires_tick},
		     {"schema", impl_->knowledge.definition().schemas.at(message.value.payload.schema - 1).name},
		     {"request_key", message.value.request_key}});
	return result;
}
} // namespace phoenix
