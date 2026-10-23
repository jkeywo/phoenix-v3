// Compiled mission authoring; included by the scenario definition compiler.
state_tree::Value condition_value(const Json &v)
{
	if (v.is_boolean())
		return state_tree::Value::boolean(v.get<bool>());
	if (v.is_number_integer())
		return state_tree::Value::integer(v.get<std::int64_t>());
	require(v.is_string() && v.get_ref<const std::string &>().size() <= 4096, "condition needs a bounded scalar");
	return state_tree::Value::string(v.get<std::string>());
}
ScenarioCondition compile_condition(const Json &v, const ScenarioDefinition &d, unsigned depth = 0,
                                    unsigned *budget = nullptr)
{
	unsigned local = 0;
	if (!budget)
		budget = &local;
	require(++*budget <= 256, "condition node limit exceeded");
	require(v.is_object() && depth < 16, "invalid/deep condition");
	ScenarioCondition c;
	for (const auto &[key, kind] :
	     std::array{std::pair{"all", ScenarioCondition::Kind::All}, std::pair{"any", ScenarioCondition::Kind::Any},
	                std::pair{"not", ScenarioCondition::Kind::Not}})
		if (v.contains(key))
		{
			require(v.size() == 1, "condition composition cannot mix fields");
			c.kind = kind;
			if (kind == ScenarioCondition::Kind::Not)
				c.children.push_back(compile_condition(v.at(key), d, depth + 1, budget));
			else
			{
				const auto &a = v.at(key);
				require(a.is_array() && !a.empty() && a.size() <= 32, "invalid condition composition");
				for (const auto &x : a)
					c.children.push_back(compile_condition(x, d, depth + 1, budget));
			}
			return c;
		}
	c.fact = identifier(v, "fact");
	require(d.fact_bindings.contains(c.fact), "unknown condition fact " + c.fact);
	const auto op = v.value("op", std::string{"eq"});
	const std::map<std::string, state_tree::Compare> ops{
	    {"eq", state_tree::Compare::Equal},       {"ne", state_tree::Compare::NotEqual},
	    {"lt", state_tree::Compare::Less},        {"le", state_tree::Compare::LessEqual},
	    {"gt", state_tree::Compare::Greater},     {"ge", state_tree::Compare::GreaterEqual},
	    {"present", state_tree::Compare::Present}};
	require(ops.contains(op), "unknown condition comparison");
	c.operation = ops.at(op);
	if (c.operation != state_tree::Compare::Present)
	{
		c.value = condition_value(v.at("value"));
		require(c.value.type == d.fact_bindings.at(c.fact).type, "condition type mismatch " + c.fact);
		require(c.operation == state_tree::Compare::Equal || c.operation == state_tree::Compare::NotEqual ||
		            c.value.type == state_tree::Type::Integer,
		        "ordered condition needs integer");
	}
	return c;
}
void compile_fact_bindings(const Json &source, ScenarioDefinition &out)
{
	for (const auto &v : list(source, "fact_bindings", 128))
	{
		ScenarioFactBinding b;
		b.id = identifier(v, "id");
		b.kind = identifier(v, "kind");
		b.field = v.value("field", std::string{});
		b.operation = v.value("operation", std::string{});
		if (b.kind == "event")
		{
			b.event = identifier(v, "event");
			b.match = parameters(v.value("match", Json::object()));
			if (v.contains("subject"))
				b.subject = actor(v.at("subject"));
		}
		else if (b.kind != "clock")
			b.subject = actor(v.at("subject"));
		if (b.kind == "entity")
		{
			require(b.field == "lifecycle" || b.field == "definition", "unknown entity fact");
			b.type = state_tree::Type::Text;
		}
		else if (b.kind == "clock")
		{
			require(b.field == "tick", "unknown clock fact");
			b.type = state_tree::Type::Integer;
		}
		else if (b.kind == "inventory")
		{
			require(valid_identifier(b.field) && valid_identifier(b.operation),
			        "inventory fact needs store and material");
			b.type = state_tree::Type::Integer;
		}
		else if (b.kind == "compartment")
		{
			require(valid_identifier(b.field), "invalid compartment fact");
		}
		else if (b.kind == "away")
		{
			require(valid_identifier(b.operation), "away fact needs operation");
			require(b.field == "phase" || b.field == "outcome" || b.field == "ever_deployed" ||
			            b.field == "recovered" || b.field == "lost",
			        "unknown away fact");
			if (b.field == "phase" || b.field == "outcome")
				b.type = state_tree::Type::Text;
		}
		else if (b.kind == "objective")
		{
			require((b.field == "status" || b.field == "completed_tick") && valid_identifier(b.operation),
			        "invalid objective fact");
			b.type = b.field == "status" ? state_tree::Type::Text : state_tree::Type::Integer;
		}
		else if (b.kind == "personnel")
		{
			require(b.field == "deployed" || b.field == "lost" || b.field == "recovered", "unknown personnel fact");
			b.type = state_tree::Type::Integer;
		}
		else
			require(b.kind == "event", "unknown fact kind " + b.kind);
		require(out.fact_bindings.emplace(b.id, std::move(b)).second, "duplicate fact binding");
	}
}
void compile_mission_reports(const Json &source, ScenarioDefinition &out)
{
	const std::set<std::string> kinds{
	    "ftl.ready",           "away.deployed",    "away.work_completed", "away.completed",     "away.failed",
	    "away.available",      "away.recovered",   "personnel.lost",      "entity.destroyed",   "entity.departed",
	    "objective.completed", "objective.failed", "objective.active",    "objective.cancelled"};
	for (const auto &v : list(source, "event_bindings", 128))
	{
		ScenarioEventBinding b;
		b.kind = identifier(v, "kind");
		b.event = identifier(v, "event");
		b.operation = v.value("operation", std::string{});
		b.detail = v.value("detail", std::string{});
		if (v.contains("subject"))
			b.subject = actor(v.at("subject"));
		require(kinds.contains(b.kind) && contains_name(out.events, b.event), "invalid transition event binding");
		out.event_bindings.push_back(std::move(b));
	}
	for (const auto &[id, b] : out.fact_bindings)
		if (b.kind == "event")
			require(contains_name(out.events, b.event), "undeclared event fact");
	std::set<std::string> ids;
	for (const auto &v : list(source, "reports", 64))
	{
		ScenarioReportDefinition r;
		r.id = identifier(v, "id");
		require(ids.insert(r.id).second, "duplicate report");
		r.ship = actor(v.at("ship"));
		r.instance = v.value("instance", std::string{"main"});
		r.title_key = text(v, "title_key");
		for (const auto &o : list(v, "objectives", 256))
		{
			require(o.is_string() && out.objectives.contains(o.get<std::string>()), "unknown report objective");
			r.objectives.push_back(o.get<std::string>());
		}
		for (const auto &f : list(v, "facts", 128))
		{
			auto id = identifier(f, "fact");
			require(out.fact_bindings.contains(id), "unknown report fact");
			r.facts.emplace_back(id, text(f, "label_key"));
		}
		for (const auto &rule : list(v, "rules", 32))
			r.rules.push_back({identifier(rule, "result"), text(rule, "title_key"), text(rule, "explanation_key"),
			                   compile_condition(rule.at("when"), out)});
		const auto &f = v.at("fallback");
		r.fallback = identifier(f, "result");
		r.fallback_title_key = text(f, "title_key");
		r.fallback_explanation_key = text(f, "explanation_key");
		std::set<std::string> assessment_ids;
		for (const auto &a : list(v, "assessments", 16))
		{
			ScenarioReportDefinition assessment;
			assessment.id = identifier(a, "id");
			require(assessment_ids.insert(assessment.id).second, "duplicate report assessment");
			assessment.title_key = text(a, "title_key");
			for (const auto &rule : list(a, "rules", 32))
				assessment.rules.push_back({identifier(rule, "result"), text(rule, "title_key"),
				                            text(rule, "explanation_key"), compile_condition(rule.at("when"), out)});
			const auto &fallback = a.at("fallback");
			assessment.fallback = identifier(fallback, "result");
			assessment.fallback_title_key = text(fallback, "title_key");
			assessment.fallback_explanation_key = text(fallback, "explanation_key");
			r.assessments.push_back(std::move(assessment));
		}
		out.reports.push_back(std::move(r));
	}
}
