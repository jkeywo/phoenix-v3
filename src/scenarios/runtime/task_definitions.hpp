// Private compiler helpers; the canonical task declarations belong to scenario.
void compile_tasks(const Json &source, ScenarioDefinition &out)
{
	if (!source.contains("task_plan"))
		return;
	require(source.contains("tree"), "task_plan requires a root scenario tree for its shared bindings and events");
	const auto &plan = source.at("task_plan");
	require(plan.is_object(), "task_plan needs an object");
	out.task_root = identifier(plan, "root");
	out.task_seed = static_cast<std::uint64_t>(number(plan, "seed", 1, 1, 2147483647));
	out.task_random_seed = plan.value("random_seed", false);
	if (plan.contains("limits"))
	{
		const auto &limits = plan.at("limits");
		out.task_depth = static_cast<std::uint32_t>(number(limits, "depth", 16, 1, 64));
		out.task_nodes = static_cast<std::uint32_t>(number(limits, "nodes", 256, 1, 4096));
		out.task_expansions = static_cast<std::uint32_t>(number(limits, "expansions", 2048, 1, 65536));
	}
	for (const auto &row : list(plan, "tasks", 4096))
	{
		ScenarioTaskDefinition task;
		task.id = identifier(row, "id");
		task.vary_methods = row.value("vary_methods", false);
		for (const auto &objective : list(row, "objectives"))
		{
			const auto id = objective.get<std::string>();
			require(out.objectives.contains(id), "task " + task.id + " references unknown objective " + id);
			task.objectives.push_back(id);
		}
		for (const auto &[field, target] :
		     std::array{std::pair{"opportunities", &task.opportunities}, std::pair{"liabilities", &task.liabilities}})
			for (const auto &fact : list(row, field))
			{
				const auto id = fact.get<std::string>();
				require(out.fact_bindings.contains(id), "task circumstance needs a declared fact " + id);
				target->push_back(id);
			}
		if (row.contains("participants"))
		{
			require(row.at("participants").is_object() && row.at("participants").size() <= 32,
			        "task participants need a bounded object");
			for (auto p = row.at("participants").begin(); p != row.at("participants").end(); ++p)
			{
				require(valid_identifier(p.key()), "invalid task participant name");
				if (p.value().contains("fact"))
				{
					const auto fact = identifier(p.value(), "fact");
					require(p.value().size() == 1 && out.fact_bindings.contains(fact) &&
					            out.fact_bindings.at(fact).type == state_tree::Type::Entity,
					        "task participant fact must be an explicitly typed entity binding");
					task.participants.emplace(p.key(), ScenarioActor{ScenarioActorKind::Fact, 0, fact});
				}
				else
					task.participants.emplace(p.key(), actor(p.value()));
			}
		}
		if (row.contains("complete_when"))
			task.complete_when = compile_condition(row.at("complete_when"), out);
		if (row.contains("fail_when"))
			task.fail_when = compile_condition(row.at("fail_when"), out);
		std::set<std::string> methods;
		for (const auto &m : list(row, "methods", 64))
		{
			ScenarioTaskMethod method;
			method.id = identifier(m, "id");
			require(methods.insert(method.id).second, "duplicate task method " + method.id);
			method.situation = m.value("situation", std::string{});
			if (!method.situation.empty())
				require(valid_identifier(method.situation), "invalid task situation");
			if (m.contains("requires"))
				method.prerequisites = compile_condition(m.at("requires"), out);
			if (m.contains("maintain"))
				method.maintain = compile_condition(m.at("maintain"), out);
			if (m.contains("release_when"))
				method.release_when = compile_condition(m.at("release_when"), out);
			method.earliest = number(m, "earliest_ticks", 0, 0, 1000000000);
			method.latest = number(m, "latest_ticks", 0, 0, 1000000000);
			method.duration = number(m, "duration_ticks", 0, 0, 1000000000);
			method.optional = m.value("optional", false);
			require(!method.latest || method.latest >= method.earliest, "task scheduling window is reversed");
			if (m.contains("attention"))
			{
				require(m.at("attention").is_array() && m.at("attention").size() <= 32,
				        "task attention needs a bounded demand array");
				for (const auto &a : m.at("attention"))
				{
					ScenarioTaskAttention demand{identifier(a, "participant"), identifier(a, "console"),
					                             number(a, "amount", 0, 0, 100)};
					require(task.participants.contains(demand.participant),
					        "attention demand names an undeclared participant");
					method.attention.push_back(std::move(demand));
				}
			}
			std::set<std::string> children;
			for (const auto &c : list(m, "children", 256))
			{
				ScenarioTaskChild child;
				child.id = identifier(c, "id");
				require(child.id.find('.') == std::string::npos, "local task child IDs cannot contain dots");
				child.task = identifier(c, "task");
				require(children.insert(child.id).second, "duplicate task child " + child.id);
				for (const auto &a : list(c, "after", 256))
				{
					const auto dependency = a.get<std::string>();
					// Requiring earlier siblings makes dependencies acyclic and execution
					// order explicit, while allowing any number of concurrent siblings.
					require(dependency != child.id && children.contains(dependency),
					        "task dependency must name an earlier sibling");
					child.after.push_back(dependency);
				}
				method.children.push_back(std::move(child));
			}
			require(method.situation.empty() != method.children.empty(),
			        "task method needs either a situation or children");
			task.methods.push_back(std::move(method));
		}
		require(!task.methods.empty(), "task needs at least one method");
		require(out.tasks.emplace(task.id, std::move(task)).second, "duplicate task definition");
	}
	require(out.tasks.contains(out.task_root), "task root does not exist");
	std::set<std::string> visiting, done;
	const auto visit = [&](const auto &self, const std::string &id) -> void
	{
		require(out.tasks.contains(id), "unknown child task " + id);
		if (done.contains(id))
			return;
		require(visiting.insert(id).second, "recursive task decomposition " + id);
		require(visiting.size() <= 64, "task definition depth exceeds 64");
		for (const auto &method : out.tasks.at(id).methods)
			for (const auto &child : method.children)
				self(self, child.task);
		visiting.erase(id);
		done.insert(id);
	};
	for (const auto &[id, task] : out.tasks)
		visit(visit, id);
}
