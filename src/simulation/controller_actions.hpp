// World::Impl: shared action execution, independent of the selecting policy.
const ControllerActionSpec &controller_action_spec(const ConsoleControllerRuntime &controller) const
{
	for (const auto &spec : controller.brain->actions) if (spec.id == controller.action.id) return spec;
	throw std::runtime_error("Unknown active Controller action");
}
std::vector<state_tree::Value> action_observations(const ConsoleControllerRuntime &controller, std::span<const state_tree::Value> source) const
{
	std::vector<state_tree::Value> result(source.begin(), source.end());
	const auto &spec = controller_action_spec(controller);
	for (std::size_t i = 0; i < spec.inputs.size(); ++i) result.at(spec.inputs[i]) = controller.action.inputs.at(i);
	return result;
}
void cancel_controller_action(EntityId id, std::size_t index, const std::string &reason, std::span<const state_tree::Value> source)
{
	auto &controller = entities.at(id).get_mut<EntityControls>().controllers.at(index);
	if (controller.action.status != "running") return;
	const auto &spec = controller_action_spec(controller);
	const auto observations = action_observations(controller, source);
	const auto result = state_tree::interrupt(spec.program, controller.action.continuation, observations);
	for (const auto &output : result.outputs) controller_output(id, index, output, observations, &spec.program);
	controller.action.status = "cancelled";
	controller.action.reason = reason;
}
bool select_controller_action(EntityId id, std::size_t index, const std::string &name, const std::string &invocation,
                              std::span<const state_tree::Value> inputs, std::span<const state_tree::Value> source, std::uint64_t generation)
{
	auto &controller = entities.at(id).get_mut<EntityControls>().controllers.at(index);
	if (controller.human || generation != controller.generation || !controller.brain || invocation.empty() || invocation.size() > 128) return false;
	const auto found = std::find_if(controller.brain->actions.begin(), controller.brain->actions.end(), [&](const auto &spec) { return spec.id == name; });
	if (found == controller.brain->actions.end() || inputs.size() != found->inputs.size()) return false;
	for (std::size_t i = 0; i < inputs.size(); ++i)
		if (inputs[i].type != controller.brain->program.registry.observations.at(found->inputs[i]).type) return false;
	auto &active = controller.action;
	const std::vector<state_tree::Value> arguments(inputs.begin(), inputs.end());
	if (active.id == name && active.invocation == invocation && active.inputs == arguments) return true;
	std::vector<state_tree::Value> observations(source.begin(), source.end());
	for (std::size_t i = 0; i < inputs.size(); ++i) observations.at(found->inputs[i]) = inputs[i];
	state_tree::Instance check;
	const auto availability = state_tree::step(found->availability, check, observations);
	if (availability.debug.active.empty() || availability.debug.active.back() != "available") return false;
	cancel_controller_action(id, index, "replaced", source);
	if (active.serial == std::numeric_limits<std::uint64_t>::max()) throw std::runtime_error("Action serial exhausted");
	const auto serial = active.serial + 1;
	active = {name, invocation, "running", {}, arguments, {}, serial};
	return true;
}
void run_controller_action(EntityId id, std::size_t index, std::span<const state_tree::Value> source)
{
	auto &controller = entities.at(id).get_mut<EntityControls>().controllers.at(index);
	if (controller.action.status != "running") return;
	const auto &spec = controller_action_spec(controller);
	const auto observations = action_observations(controller, source);
	const auto result = state_tree::step(spec.program, controller.action.continuation, observations);
	for (const auto &output : result.outputs)
	{
		controller_output(id, index, output, observations, &spec.program);
		if (controller.action.status != "running") break;
	}
	if (controller.action.status != "running")
	{
		const auto status = controller.action.status, reason = controller.action.reason;
		const auto cleanup = state_tree::interrupt(spec.program, controller.action.continuation, observations);
		for (const auto &output : cleanup.outputs) controller_output(id, index, output, observations, &spec.program);
		controller.action.status = status; controller.action.reason = reason;
	}
}
Json save_controller_action(const ConsoleControllerRuntime &controller) const
{
	const auto &a = controller.action;
	Json result{{"id", a.id}, {"invocation", a.invocation}, {"status", a.status}, {"reason", a.reason}, {"serial", a.serial}, {"inputs", Json::array()}};
	for (const auto &v : a.inputs) result["inputs"].push_back({{"type", static_cast<int>(v.type)}, {"number", v.number}, {"text", v.text}});
	if (!a.id.empty()) result["continuation"] = state_tree::save(controller_action_spec(controller).program, a.continuation);
	return result;
}
void restore_controller_action(ConsoleControllerRuntime &controller, const Json &saved)
{
	auto &a = controller.action;
	a.id = saved.at("id"); a.invocation = saved.at("invocation"); a.status = saved.at("status"); a.reason = saved.at("reason"); a.serial = saved.at("serial");
	if (a.reason.size() > 4096 || a.invocation.size() > 128 || (a.status != "idle" && a.status != "running" && a.status != "succeeded" && a.status != "failed" && a.status != "cancelled")) throw std::runtime_error("Invalid action status");
	if (a.id.empty())
	{
		if (a.status != "idle" || a.serial || !saved.at("inputs").empty() || saved.contains("continuation")) throw std::runtime_error("Invalid idle action");
		return;
	}
	if (!controller.brain || !a.serial || a.invocation.empty() || a.status == "idle") throw std::runtime_error("Invalid action ownership");
	const auto &spec = controller_action_spec(controller);
	if (!saved.at("inputs").is_array() || saved.at("inputs").size() != spec.inputs.size()) throw std::runtime_error("Invalid saved action inputs");
	a.inputs.clear();
	for (std::size_t i = 0; i < spec.inputs.size(); ++i)
	{
		const auto &v = saved.at("inputs").at(i);
		const auto type = spec.program.registry.observations.at(spec.inputs[i]).type;
		if (v.at("type") != static_cast<int>(type) || !v.at("number").is_number_integer() || !v.at("text").is_string() || v.at("text").get_ref<const std::string &>().size() > 4096) throw std::runtime_error("Invalid saved action input type");
		a.inputs.push_back({type, v.at("number"), v.at("text")});
	}
	a.continuation = state_tree::load(spec.program, saved.at("continuation"));
}
