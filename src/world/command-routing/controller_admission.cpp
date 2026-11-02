module;
#include "native_headers.hpp"

module phoenix.simulation;
import :command_routing;

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
		                {"eligible", !c.candidates.empty() || (c.brain && !c.brain->objective_bindings.empty())},
		                {"human", c.human},
		                {"ai_running", controller_ai_running(c)},
		                {"selected", c.selected_provider},
		                {"effective", c.effective_provider},
		                {"reason", c.fallback_reason},
		                {"next", c.next_decision},
		                {"last", c.last_decision_tick},
		                {"sequence", c.decision_sequence}});
	return rows;
}

Json World::controller_decision_snapshot(const ControllerAddress &address, bool full_diagnostics)
{
	return impl_->model_snapshot(address, full_diagnostics);
}

bool World::configure_controller_provider(const ControllerAddress &address, const std::string &provider)
{
	impl_->clear_presentation_snapshots();
	if (provider != "tree" && provider != "anyjev" && provider != "llamacpp" && provider != "jev")
		return false;
	auto *c = impl_->model_controller(address);
	if (!c || (provider != "tree" && c->candidates.empty() && (!c->brain || c->brain->objective_bindings.empty())))
		return false;
	if (c->selected_provider == provider)
		return true;
	const auto index =
	    static_cast<std::size_t>(c - impl_->entities.at(address.entity).get_mut<EntityControls>().controllers.data());
	if (c->brain && controller_ai_running(*c) && c->brain->objective_bindings.empty())
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
		if (!c || !controller_ai_running(*c) || c->selected_provider == "tree" || snapshot.at("schema") != 1 ||
		    snapshot.at("provider") != c->selected_provider || snapshot.at("tick") > ticks() ||
		    (!decision.value("failed", false) && snapshot.at("expires") < ticks()) ||
		    snapshot.at("expires").get<std::uint64_t>() > snapshot.at("tick").get<std::uint64_t>() + 100 ||
		    snapshot.at("sequence") != c->decision_sequence + 1 ||
		    snapshot.at("basis").at("generation") != c->generation ||
		    snapshot.at("basis").at("provider_generation") != c->provider_generation ||
		    (!decision.value("failed", false) && basis != current_basis))
			return false;
		// This is the validated incoming order basis, before policy interruption or action outputs.
		const auto accepted_order_basis =
		    model_order_basis(impl_->entities.at(address.entity).get<BridgeBlackboard>(), *c);
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
		std::optional<ModelCandidate> selected;
		if (chosen != "continue")
		{
			for (auto &candidate : evaluate_model_candidates(*c, observed))
				if (candidate.id == chosen)
					selected = std::move(candidate);
			if (!selected)
				return false;
			// The supplied wire offer must still match current permitted availability.
			// Execution below retains the typed candidate and never decodes this JSON.
			const auto canonical = model_candidate_json(*c, *selected);
			bool offered = false;
			for (const auto &candidate : snapshot.at("candidates"))
				if (candidate == canonical)
					offered = true;
			if (!offered)
				return false;
		}
		if (c->effective_provider == "tree" && c->brain->objective_bindings.empty())
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
		if (selected)
			for (const auto &operation : selected->operations)
			{
				const auto &args = operation.arguments;
				ControllerDecisionProposal proposal{address, c->generation};
				using Kind = ControllerCandidateOperationKind;
				if (operation.kind == Kind::Select || operation.kind == Kind::Replace)
				{
					proposal.action = c->actions.at(*operation.action).id;
					proposal.invocation =
					    selected->id == "officer-accept" ? args.at(0).text + "/model" : "model/" + chosen;
					if (operation.kind == Kind::Replace)
						proposal.invocation += "/" + std::to_string(c->decision_sequence);
					proposal.inputs = {args.begin() + 1, args.end()};
				}
				else if (operation.kind == Kind::Cancel)
				{
					proposal.operation = "cancel";
					proposal.invocation = args.at(0).text;
				}
				else if (operation.kind == Kind::Acknowledge)
				{
					impl_->knowledge.acknowledge_controller(address, args.at(0).number);
					continue;
				}
				else
				{
					proposal.operation = operation.name;
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
		                               {"deviation", selected ? selected->deviation : std::string{}}});
		if (c->decision_history.size() > 16)
			c->decision_history.erase(c->decision_history.begin());
		record_model_deviation_receipt(*c, accepted_order_basis, selected ? selected->deviation : std::string{});
		if (selected && !selected->deviation.empty())
			impl_->model_deviation(address, *c, selected->deviation);
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
