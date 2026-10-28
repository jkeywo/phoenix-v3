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
Json World::controller_ai(EntityId id) const
{
	Json rows = Json::array();
	auto entity = impl_->entities.find(id);
	if (entity == impl_->entities.end() || !entity->second.has<EntityControls>())
		return rows;
	for (const auto &c : entity->second.get<EntityControls>().controllers)
		rows.push_back({{"ship", id},
		                {"console", c.definition.console},
		                {"controller", c.definition.id},
		                {"eligible", !c.candidates.empty()},
		                {"human", c.human},
		                {"selected", c.selected_provider},
		                {"effective", c.effective_provider},
		                {"reason", c.fallback_reason},
		                {"next", c.next_decision},
		                {"last", c.last_decision_tick},
		                {"sequence", c.decision_sequence}});
	return rows;
}

Json World::controller_decision_snapshot(const ControllerAddress &address)
{
	return impl_->model_snapshot(address);
}

bool World::configure_controller_provider(const ControllerAddress &address, const std::string &provider)
{
	impl_->clear_presentation_snapshots();
	if (provider != "tree" && provider != "anyjev" && provider != "llamacpp" && provider != "jev")
		return false;
	auto *c = impl_->model_controller(address);
	if (!c || (provider != "tree" && c->candidates.empty()))
		return false;
	if (c->selected_provider == provider)
		return true;
	const auto index =
	    static_cast<std::size_t>(c - impl_->entities.at(address.entity).get_mut<EntityControls>().controllers.data());
	if (c->brain && !c->human)
	{
		const auto observations = impl_->controller_observations(address.entity, *c);
		impl_->cancel_controller_action(address.entity, index, "provider_changed", observations);
		const auto exit = state_tree::interrupt(c->brain->program, c->brain->continuation, observations);
		for (const auto &output : exit.outputs)
			impl_->controller_output(address.entity, index, output, observations);
	}
	++c->provider_generation;
	c->selected_provider = provider;
	c->effective_provider = "tree";
	c->fallback_reason = provider == "tree" ? "" : "connecting";
	c->next_decision = ticks();
	c->retry_count = 0;
	return true;
}

bool World::submit_controller_decision(const Json &decision)
{
	impl_->clear_presentation_snapshots();
	try
	{
		const auto &snapshot = decision.at("snapshot");
		ControllerAddress address{snapshot.at("ship"), snapshot.at("console"), snapshot.at("controller")};
		auto *c = impl_->model_controller(address);
		if (c && !c->human && impl_->encounter_owns_controller(address.entity, c->definition))
			return false;
		const auto chosen = decision.value("candidate", std::string("defer_to_tree"));
		bool cancels_activity = false;
		for (const auto &candidate : snapshot.at("candidates"))
			if (candidate.at("id") == chosen)
				for (const auto &operation : candidate.value("operations", Json::array()))
					cancels_activity = cancels_activity || operation.value("name", std::string{}) == "action.cancel";
		auto basis = snapshot.at("basis");
		auto current_basis = c ? impl_->model_basis(address.entity, *c) : Json();
		if (c && c->effective_provider == "tree" && chosen != "continue" && chosen != "defer_to_tree" &&
		    !cancels_activity)
		{
			// Tree adaptation must not permanently prevent a freshly revalidated concrete takeover.
			basis.erase("action_serial");
			basis.erase("action_status");
			current_basis.erase("action_serial");
			current_basis.erase("action_status");
		}
		if (!c || c->human || c->selected_provider == "tree" || snapshot.at("schema") != 1 ||
		    snapshot.at("provider") != c->selected_provider || snapshot.at("tick") > ticks() ||
		    (!decision.value("failed", false) && snapshot.at("expires") < ticks()) ||
		    snapshot.at("expires").get<std::uint64_t>() > snapshot.at("tick").get<std::uint64_t>() + 100 ||
		    snapshot.at("sequence") != c->decision_sequence + 1 ||
		    snapshot.at("basis").at("generation") != c->generation ||
		    snapshot.at("basis").at("provider_generation") != c->provider_generation ||
		    (!decision.value("failed", false) && basis != current_basis))
			return false;
		const auto interval = checked_unsigned(decision.value("interval_ticks", Json(40)), 10, 40, "decision interval");
		const auto index = static_cast<std::size_t>(
		    c - impl_->entities.at(address.entity).get_mut<EntityControls>().controllers.data());
		if (decision.value("failed", false) || chosen == "defer_to_tree")
		{
			++c->decision_sequence;
			impl_->model_fallback(address.entity, index,
			                      !decision.value("failed", false) && chosen == "defer_to_tree"
			                          ? "provider_deferred"
			                          : decision.value("reason", std::string("provider_failed")));
			return true;
		}
		const auto observed = impl_->controller_observations(address.entity, *c);
		Json selected;
		if (chosen != "continue")
		{
			for (const auto &candidate : model_candidates(*c, observed))
				if (candidate.at("id") == chosen)
					selected = candidate;
			if (selected.is_null())
				return false;
			bool offered = false;
			for (const auto &candidate : snapshot.at("candidates"))
				if (candidate == selected)
					offered = true;
			if (!offered)
				return false;
		}
		if (c->effective_provider == "tree")
		{
			const auto exit = state_tree::interrupt(c->brain->program, c->brain->continuation, observed);
			for (const auto &output : exit.outputs)
				impl_->controller_output(address.entity, index, output, observed);
		}
		c->effective_provider = c->selected_provider;
		c->fallback_reason.clear();
		c->retry_count = 0;
		c->next_decision = std::max<std::uint64_t>(ticks(), snapshot.at("tick").get<std::uint64_t>() + interval);
		c->last_decision_tick = ticks();
		++c->decision_sequence;
		bool accepted = true;
		if (!selected.is_null())
			for (const auto &operation : selected.at("operations"))
			{
				const auto name = operation.at("name").get<std::string>();
				std::vector<state_tree::Value> args;
				for (const auto &v : operation.at("arguments"))
					args.push_back(
					    {static_cast<state_tree::Type>(v.at("type").get<int>()), v.at("number"), v.at("text")});
				ControllerDecisionProposal proposal{address, c->generation};
				if (name.starts_with("select.") || name.starts_with("replace."))
				{
					proposal.action = name.substr(name.starts_with("select.") ? 7 : 8);
					proposal.invocation =
					    selected.at("id") == "officer-accept" ? args[0].text + "/model" : "model/" + chosen;
					if (name.starts_with("replace."))
						proposal.invocation += "/" + std::to_string(c->decision_sequence);
					proposal.inputs = {args.begin() + 1, args.end()};
				}
				else if (name == "action.cancel")
				{
					proposal.operation = "cancel";
					proposal.invocation = args[0].text;
				}
				else if (name == "acknowledge_message")
				{
					impl_->knowledge.acknowledge_controller(address, args[0].number);
					continue;
				}
				else
				{
					proposal.operation = name;
					proposal.inputs = args;
				}
				if (!impl_->admit_controller_decision(proposal, observed))
				{
					accepted = false;
					break;
				}
			}
		c->decision_history.push_back({{"orders", snapshot.at("basis").at("orders")},
		                               {"tick", ticks()},
		                               {"candidate", chosen},
		                               {"accepted", accepted},
		                               {"deviation", selected.is_null() ? Json("") : selected.at("deviation")}});
		if (c->decision_history.size() > 16)
			c->decision_history.erase(c->decision_history.begin());
		if (!selected.is_null() && !selected.at("deviation").get<std::string>().empty())
			impl_->model_deviation(address, *c, selected.at("deviation"));
		if (!accepted)
			impl_->model_fallback(address.entity, index, "decision_refused");
		return true;
	}
	catch (const std::exception &)
	{
		return false;
	}
}
} // namespace phoenix
