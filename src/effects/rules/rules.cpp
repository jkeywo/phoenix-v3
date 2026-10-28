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

module phoenix.effects.rules;

namespace phoenix
{
namespace
{
using Type = state_tree::Type;
using Value = state_tree::Value;
using Op = RuleExpressionOp;
bool checked_add(std::int64_t a, std::int64_t b, std::int64_t &out)
{
	if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b) ||
	    (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b))
		return false;
	out = a + b;
	return true;
}

bool checked_multiply(std::int64_t a, std::int64_t b, std::int64_t &out)
{
	if (a > 0 ? (b > 0 ? a > std::numeric_limits<std::int64_t>::max() / b
	                   : b < std::numeric_limits<std::int64_t>::min() / a)
	          : (a < 0 && (b > 0 ? a < std::numeric_limits<std::int64_t>::min() / b
	                             : b < 0 && a < std::numeric_limits<std::int64_t>::max() / b)))
		return false;
	out = a * b;
	return true;
}
} // namespace

bool evaluate_rule_expression(const RuleExpression &expression, const RuleContext &context, Value &out)
{
	if (expression.nodes.empty() || expression.nodes.size() > rule_expression_node_limit ||
	    expression.root >= expression.nodes.size())
		return false;
	std::vector<Value> values;
	values.reserve(expression.nodes.size());
	for (const auto &node : expression.nodes)
	{
		if (node.op == Op::Literal)
		{
			values.push_back(node.literal);
			continue;
		}
		if (node.op == Op::Fact)
		{
			if (node.fact >= context.facts.size())
				return false;
			values.push_back(context.facts[node.fact]);
			continue;
		}
		if (node.args.size() > 32)
			return false;
		std::array<const Value *, 32> argument_buffer;
		std::size_t count = 0;
		for (const auto index : node.args)
		{
			if (index >= values.size())
				return false;
			argument_buffer[count++] = &values[index];
		}
		const std::span<const Value *const> args(argument_buffer.data(), count);
		if (args.empty())
			return false;
		Value value;
		if (node.op == Op::Present)
		{
			if (args.size() != 1)
				return false;
			value = Value::boolean(args[0]->type != Type::Missing);
		}
		else if (node.op == Op::All || node.op == Op::Any || node.op == Op::Not)
		{
			bool b = node.op == Op::All;
			for (const auto &a : args)
			{
				if (a->type != Type::Boolean)
					return false;
				if (node.op == Op::All)
					b = b && a->number;
				else if (node.op == Op::Any)
					b = b || a->number;
			}
			if (node.op == Op::Not)
			{
				if (args.size() != 1)
					return false;
				b = !args[0]->number;
			}
			value = Value::boolean(b);
		}
		else if (node.op >= Op::Equal && node.op <= Op::GreaterEqual)
		{
			if (args.size() != 2 || args[0]->type != args[1]->type)
				return false;
			const auto comparison =
			    static_cast<state_tree::Compare>(static_cast<int>(node.op) - static_cast<int>(Op::Equal));
			value = Value::boolean(state_tree::compare_values(*args[0], comparison, *args[1]));
		}
		else
		{
			for (const auto &a : args)
				if (a->type != Type::Integer)
					return false;
			if (args.size() < 2)
				return false;
			auto n = args[0]->number;
			const auto b = args[1]->number;
			if (node.op == Op::Add)
			{
				if (!checked_add(n, b, n))
					return false;
			}
			else if (node.op == Op::Subtract)
			{
				if ((b > 0 && n < std::numeric_limits<std::int64_t>::min() + b) ||
				    (b < 0 && n > std::numeric_limits<std::int64_t>::max() + b))
					return false;
				n -= b;
			}
			else if (node.op == Op::Multiply)
			{
				if (!checked_multiply(n, b, n))
					return false;
			}
			else if (node.op == Op::Divide)
			{
				if (!b || (n == std::numeric_limits<std::int64_t>::min() && b == -1))
					return false;
				n /= b;
			}
			else if (node.op == Op::Ratio)
			{
				if (args.size() != 3 || !checked_multiply(n, args[2]->number, n) || !b ||
				    (n == std::numeric_limits<std::int64_t>::min() && b == -1))
					return false;
				n /= b;
			}
			else if (node.op == Op::Min || node.op == Op::Max)
			{
				for (const auto &a : args)
					n = node.op == Op::Min ? std::min(n, a->number) : std::max(n, a->number);
			}
			else if (node.op == Op::Clamp)
			{
				if (args.size() != 3 || b > args[2]->number)
					return false;
				n = std::clamp(n, b, args[2]->number);
			}
			else
				return false;
			value = Value::integer(n);
		}
		values.push_back(std::move(value));
	}
	out = values[expression.root];
	return out.type == expression.type;
}

RuleEvaluation evaluate_rule(const RuleDefinition &rule, const RuleContext &context)
{
	if (context.facts.size() != rule.facts.size())
		return {};
	for (std::size_t i = 0; i < rule.facts.size(); ++i)
		if (context.facts[i].type != rule.facts[i].type && context.facts[i].type != Type::Missing)
			return {};
	Value value;
	if (rule.condition)
	{
		if (!evaluate_rule_expression(*rule.condition, context, value))
			return {};
		if (!value.number)
			return {RuleEvaluationStatus::Unmatched, {}};
	}
	RuleEvaluation evaluation;
	auto &result = evaluation.quote.effects;
	result = rule.effects;
	static const std::map<RuleRole, std::string> roles{
	    {RuleRole::Owner, "owner"},        {RuleRole::Source, "source"}, {RuleRole::Instigator, "instigator"},
	    {RuleRole::Subject, "subject"},    {RuleRole::Target, "target"}, {RuleRole::Attacker, "attacker"},
	    {RuleRole::Protected, "protected"}};
	for (const auto &cost : rule.costs)
	{
		if (!evaluate_rule_expression(cost.amount, context, value) || value.number < 0 || value.number > resource_limit)
			return {};
		result.costs.push_back(
		    {roles.at(cost.payer), cost.equipment, cost.store, cost.resource, value.number, cost.property});
	}
	if (rule.energy)
	{
		if (!evaluate_rule_expression(*rule.energy, context, value) || value.number < 0 || value.number > 1000000000)
			return {};
		result.energy = value.number;
	}
	for (const auto &[index, expressions] : rule.values)
	{
		if (index >= result.operations.size())
			return {};
		if (expressions.damage)
		{
			if (!evaluate_rule_expression(*expressions.damage, context, value) || value.number < 0 ||
			    value.number > 1000000)
				return {};
			result.operations[index].damage = value.number;
		}
		if (expressions.amount)
		{
			if (!evaluate_rule_expression(*expressions.amount, context, value) ||
			    value.number <
			        (result.operations[index].kind == EffectOperationKind::PropertyRate ? -resource_limit : 0) ||
			    value.number > resource_limit)
				return {};
			result.operations[index].amount = value.number;
		}
		if (expressions.value)
		{
			if (!evaluate_rule_expression(*expressions.value, context, value))
				return {};
			if (value.type == Type::Integer)
				result.operations[index].value = value.number;
			else if (value.type == Type::Boolean)
				result.operations[index].value = bool(value.number);
			else if (value.type == Type::Text)
				result.operations[index].value = value.text;
			else
				return {};
		}
		if (expressions.delta_permille)
		{
			if (!evaluate_rule_expression(*expressions.delta_permille, context, value) || value.number < -100000 ||
			    value.number > 100000)
				return {};
			result.operations[index].delta_permille = value.number;
		}
	}
	if (rule.damage_permille)
	{
		if (!evaluate_rule_expression(*rule.damage_permille, context, value) || value.number < 0 || value.number > 1000)
			return {};
		evaluation.quote.damage_permille = value.number;
	}
	if (rule.funding)
		for (const auto &cost : rule.funding->costs)
		{
			if (!evaluate_rule_expression(cost.amount, context, value) || value.number < 0 ||
			    value.number > resource_limit)
				return {};
			evaluation.quote.upkeep.push_back(
			    {roles.at(cost.payer), cost.equipment, cost.store, cost.resource, value.number, cost.property});
		}
	evaluation.status = RuleEvaluationStatus::Ready;
	return evaluation;
}

std::optional<EffectGroupDefinition> materialize_rule(const RuleDefinition &rule, const RuleContext &context)
{
	auto result = evaluate_rule(rule, context);
	if (result.status != RuleEvaluationStatus::Ready)
		return {};
	return std::move(result.quote.effects);
}
} // namespace phoenix
