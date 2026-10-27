module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.content;

namespace phoenix
{
namespace
{
using Type = state_tree::Type;
using Value = state_tree::Value;
using Op = RuleExpressionOp;
[[noreturn]] void rule_fail(const std::string &at, const std::string &why)
{
	throw std::runtime_error(at + ": " + why);
}
void fields(const Json &value, const std::set<std::string> &allowed, const std::string &at)
{
	if (!value.is_object())
		rule_fail(at, "expected object");
	for (const auto &[key, unused] : value.items())
		if (!allowed.contains(key))
			rule_fail(at + "." + key, "unsupported field");
}
std::int64_t integer(const Json &value, std::int64_t minimum, std::int64_t maximum, const std::string &at)
{
	if (!value.is_number_integer() ||
	    (value.is_number_unsigned() &&
	     value.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())))
		rule_fail(at, "expected bounded integer");
	const auto number = value.get<std::int64_t>();
	if (number < minimum || number > maximum)
		rule_fail(at, "integer outside supported range");
	return number;
}
RuleRole role(const std::string &name, const std::string &at)
{
	static const std::map<std::string, RuleRole> names{
	    {"owner", RuleRole::Owner},        {"source", RuleRole::Source}, {"instigator", RuleRole::Instigator},
	    {"subject", RuleRole::Subject},    {"target", RuleRole::Target}, {"attacker", RuleRole::Attacker},
	    {"protected", RuleRole::Protected}};
	const auto found = names.find(name);
	if (found == names.end())
		rule_fail(at, "unknown context role " + name);
	return found->second;
}
RuleTrigger trigger(const std::string &name, const std::string &at)
{
	static const std::map<std::string, RuleTrigger> names{
	    {"activate", RuleTrigger::Activate},
	    {"deactivate", RuleTrigger::Deactivate},
	    {"tick", RuleTrigger::Tick},
	    {"enter", RuleTrigger::Enter},
	    {"exit", RuleTrigger::Exit},
	    {"occupancy", RuleTrigger::Occupancy},
	    {"maintain", RuleTrigger::Maintain},
	    {"pre_shield_damage", RuleTrigger::PreShieldDamage},
	    {"post_shield_damage", RuleTrigger::PostShieldDamage},
	    {"pre_compartment_damage", RuleTrigger::PreCompartmentDamage},
	    {"post_compartment_damage", RuleTrigger::PostCompartmentDamage},
	    {"pre_system_damage", RuleTrigger::PreSystemDamage},
	    {"post_system_damage", RuleTrigger::PostSystemDamage},
	    {"post_entity_damage", RuleTrigger::PostEntityDamage},
	    {"destroy", RuleTrigger::Destroy},
	    {"property_changed", RuleTrigger::PropertyChanged}};
	const auto found = names.find(name);
	if (found == names.end())
		rule_fail(at, "unknown trigger " + name);
	return found->second;
}
bool pre_damage(RuleTrigger t)
{
	return t == RuleTrigger::PreShieldDamage || t == RuleTrigger::PreCompartmentDamage ||
	       t == RuleTrigger::PreSystemDamage;
}
bool post_damage(RuleTrigger t)
{
	return t == RuleTrigger::PostShieldDamage || t == RuleTrigger::PostCompartmentDamage ||
	       t == RuleTrigger::PostSystemDamage || t == RuleTrigger::PostEntityDamage;
}
struct TypedNode
{
	std::uint32_t index;
	Type type;
	std::string unit;
};
class ExpressionCompiler
{
	const Catalog &catalog;
	SharedPropertyCatalog properties;
	RuleDefinition &rule;
	RuleExpression result;
	std::string at;
	TypedNode append(RuleExpressionNode node, Type type, std::string unit)
	{
		if (result.nodes.size() >= rule_expression_node_limit)
			rule_fail(at, "expression exceeds 256 nodes");
		const auto index = static_cast<std::uint32_t>(result.nodes.size());
		result.nodes.push_back(std::move(node));
		return {index, type, std::move(unit)};
	}
	TypedNode fact(const Json &value)
	{
		fields(value, {"fact", "property", "store", "resource", "view", "family"}, at);
		const auto name = value.at("fact").get<std::string>();
		RuleFactBinding binding;
		std::string key;
		const auto split = name.find('.');
		if (name == "tick" || name == "elapsed")
		{
			binding.kind = name == "tick" ? RuleFactKind::Tick : RuleFactKind::Elapsed;
			binding.unit = "ticks";
		}
		else if (split == std::string::npos)
			rule_fail(at, "fact requires a context role");
		else
		{
			const auto prefix = name.substr(0, split);
			key = name.substr(split + 1);
			if (prefix != "event")
				binding.role = role(prefix, at);
			if (prefix == "event" && (key == "work" || key == "activity"))
			{
				if (rule.work_unit.empty())
					rule_fail(at, "process work facts require a process evaluation");
				binding.kind = key == "work" ? RuleFactKind::ProcessWork : RuleFactKind::ProcessActivity;
				binding.unit = key == "work" ? rule.work_unit : "permille";
			}
			else if (prefix == "event" && (key == "property_before" || key == "property_after"))
			{
				if (rule.trigger != RuleTrigger::PropertyChanged)
					rule_fail(at, "property event fact requires property_changed trigger");
				binding.kind = key == "property_before" ? RuleFactKind::PropertyBefore : RuleFactKind::PropertyAfter;
				binding.key = value.value("property", std::string{});

				if (!properties->contains(binding.key))
					rule_fail(at, "unknown custom property");
				const auto &d = properties->at(binding.key);
				binding.type = d.kind == CustomPropertyKind::Flag   ? Type::Boolean
				               : d.kind == CustomPropertyKind::Enum ? Type::Text
				                                                    : Type::Integer;
				binding.unit = d.kind == CustomPropertyKind::Enum ? "enum:" + d.id : d.unit;
				binding.aux = value.value("view", std::string("effective"));
				if (binding.aux != "base" && binding.aux != "effective")
					rule_fail(at, "unknown property view");
			}
			else if (prefix == "event")
			{
				if (!pre_damage(rule.trigger) && !post_damage(rule.trigger))
					rule_fail(at, "damage fact unavailable for trigger");
				binding.unit = "damage";
				if (key == "presented")
					binding.kind = RuleFactKind::PresentedDamage;
				else if (key == "strength" && (rule.trigger == RuleTrigger::PreShieldDamage ||
				                               rule.trigger == RuleTrigger::PostShieldDamage))
				{
					binding.kind = RuleFactKind::Strength;
					binding.unit = "strength";
				}
				else if (!post_damage(rule.trigger))
					rule_fail(at, "actual damage result unavailable before resolution");
				else if (key == "inflicted" && rule.trigger != RuleTrigger::PostShieldDamage)
					binding.kind = RuleFactKind::InflictedDamage;
				else if (rule.trigger != RuleTrigger::PostShieldDamage)
					rule_fail(at, "shield result unavailable for trigger");
				else if (key == "modified")
					binding.kind = RuleFactKind::ModifiedDamage;
				else if (key == "negated")
					binding.kind = RuleFactKind::NegatedDamage;
				else if (key == "absorbed")
					binding.kind = RuleFactKind::AbsorbedDamage;
				else if (key == "residual")
					binding.kind = RuleFactKind::ResidualDamage;
				else
					rule_fail(at, "unknown damage fact " + key);
			}
			else if (key == "entity")
			{
				binding.kind = RuleFactKind::Entity;
				binding.type = Type::Entity;
			}
			else if (key == "exists")
			{
				binding.kind = RuleFactKind::Exists;
				binding.type = Type::Boolean;
			}
			else if (key == "enabled")
			{
				binding.kind = RuleFactKind::Enabled;
				binding.type = Type::Boolean;
			}
			else if (key == "eligible")
			{
				binding.kind = RuleFactKind::Eligible;
				binding.type = Type::Boolean;
			}
			else if (key == "present" || key == "healthy" || key == "on_duty")
			{
				binding.kind = key == "present"   ? RuleFactKind::OfficerPresent
				               : key == "healthy" ? RuleFactKind::OfficerHealthy
				                                  : RuleFactKind::OfficerOnDuty;
				binding.type = Type::Boolean;
			}
			else if (key == "heat")
			{
				binding.kind = RuleFactKind::Heat;
				binding.unit = "heat";
			}
			else if (key == "completed_work")
			{
				binding.kind = RuleFactKind::CompletedWork;
				binding.unit = "permille";
				binding.key = value.value("family", std::string{});
				if (!binding.key.empty() && !valid_identifier(binding.key))
					rule_fail(at, "invalid ability family");
			}
			else if (key == "output")
			{
				binding.kind = RuleFactKind::Output;
				binding.unit = "permille";
			}
			else if (key == "resource")
			{
				binding.kind = RuleFactKind::ResourceQuantity;
				binding.key = value.value("store", std::string{});
				binding.aux = value.value("resource", std::string{});
				if (binding.key.empty() || binding.key.size() > 256 || !catalog.resources.contains(binding.aux))
					rule_fail(at, "resource fact requires a store and known resource");
				binding.unit = "resource:" + binding.aux;
			}
			else if (key == "property")
			{
				binding.kind = RuleFactKind::Property;
				binding.key = value.value("property", std::string{});
				const auto &descriptors = modifier_properties();
				const auto found = std::find_if(descriptors.begin(), descriptors.end(),
				                                [&](const auto &d)
				                                {
					                                return d.id == binding.key;
				                                });
				if (found == descriptors.end())
				{

					if (!properties->contains(binding.key))
						rule_fail(at, "unknown property " + binding.key);
					const auto &d = properties->at(binding.key);
					binding.type = d.kind == CustomPropertyKind::Flag   ? Type::Boolean
					               : d.kind == CustomPropertyKind::Enum ? Type::Text
					                                                    : Type::Integer;
					binding.unit = d.kind == CustomPropertyKind::Enum ? "enum:" + d.id : d.unit;
				}
				else
					binding.unit = found->unit;
				binding.aux = value.value("view", std::string("effective"));
				if (binding.aux != "base" && binding.aux != "effective")
					rule_fail(at, "unknown property view");
			}
			else
				rule_fail(at, "unknown context fact " + key);
		}
		if ((value.contains("property") && key != "property" && key != "property_before" && key != "property_after") ||
		    ((value.contains("store") || value.contains("resource")) && key != "resource"))
			rule_fail(at, "fact metadata does not match fact kind");
		if ((value.contains("view") && key != "property" && key != "property_before" && key != "property_after") ||
		    (value.contains("family") && key != "completed_work"))
			rule_fail(at, "fact metadata does not match fact kind");
		auto found = std::find(rule.facts.begin(), rule.facts.end(), binding);
		if (found == rule.facts.end())
		{
			if (rule.facts.size() >= 256)
				rule_fail(at, "rule exceeds 256 facts");
			rule.facts.push_back(binding);
			found = rule.facts.end() - 1;
		}
		RuleExpressionNode node;
		node.op = Op::Fact;
		node.fact = static_cast<std::uint32_t>(found - rule.facts.begin());
		return append(std::move(node), binding.type, binding.unit);
	}
	TypedNode expression(const Json &value, std::size_t depth)
	{
		if (depth > 16)
			rule_fail(at, "expression exceeds 16 levels");
		if (value.is_object() && value.contains("fact"))
			return fact(value);
		if (!value.is_object() || value.contains("value"))
		{
			const Json *literal = &value;
			std::string unit;
			if (value.is_object())
			{
				fields(value, {"value", "unit", "enum"}, at);
				literal = &value.at("value");
				unit = value.value("unit", std::string{});
			}
			RuleExpressionNode node;
			if (literal->is_boolean())
				node.literal = Value::boolean(literal->get<bool>());
			else if (literal->is_number_integer())
				node.literal = Value::integer(integer(*literal, std::numeric_limits<std::int64_t>::min(),
				                                      std::numeric_limits<std::int64_t>::max(), at));
			else if (literal->is_string())
			{
				const auto s = literal->get<std::string>();
				if (s.size() > 256)
					rule_fail(at, "text exceeds 256 bytes");
				node.literal = Value::string(s);
			}
			else if (!literal->is_null())
				rule_fail(at, "expected typed scalar literal");
			if (value.is_object() && value.contains("enum"))
			{
				const auto id = value.at("enum").get<std::string>();
				const auto &definitions = properties;
				if (!unit.empty() || !definitions->contains(id) || definitions->at(id).kind != CustomPropertyKind::Enum)
					rule_fail(at, "literal requires a known enum domain");
				(void)read_custom_property_value(definitions->at(id), *literal, at);
				return append(std::move(node), Type::Text, "enum:" + id);
			}
			if (!unit.empty() && node.literal.type != Type::Integer)
				rule_fail(at, "only integers have units");
			if (!unit.empty())
			{
				bool known = unit == "ticks" || unit == "milliticks" || unit == "damage" || unit == "strength" ||
				             unit == "energy" || unit == "heat" || unit == "permille" ||
				             (!rule.work_unit.empty() && unit == rule.work_unit);
				for (const auto &d : modifier_properties())
					known = known || d.unit == unit;

				for (const auto &[id, d] : *properties)
					known = known || d.unit == unit;
				if (unit.starts_with("resource:"))
					known = catalog.resources.contains(unit.substr(9));
				if (!known)
					rule_fail(at, "unknown unit " + unit);
			}
			const auto type = node.literal.type;
			return append(std::move(node), type, std::move(unit));
		}
		fields(value, {"op", "args"}, at);
		static const std::map<std::string, Op> operations{
		    {"all", Op::All},           {"any", Op::Any},         {"not", Op::Not},      {"eq", Op::Equal},
		    {"ne", Op::NotEqual},       {"lt", Op::Less},         {"le", Op::LessEqual}, {"gt", Op::Greater},
		    {"ge", Op::GreaterEqual},   {"present", Op::Present}, {"add", Op::Add},      {"subtract", Op::Subtract},
		    {"multiply", Op::Multiply}, {"divide", Op::Divide},   {"ratio", Op::Ratio},  {"min", Op::Min},
		    {"max", Op::Max},           {"clamp", Op::Clamp}};
		const auto name = value.value("op", std::string{});
		const auto found = operations.find(name);
		if (found == operations.end())
			rule_fail(at, "unknown expression operator " + name);
		const auto &args = value.at("args");
		const auto op = found->second;
		const std::size_t arity = (op == Op::Not || op == Op::Present)   ? 1
		                          : (op == Op::Clamp || op == Op::Ratio) ? 3
		                                                                 : 2;
		const bool variadic = op == Op::All || op == Op::Any || op == Op::Min || op == Op::Max;
		if (!args.is_array() || args.size() < arity || args.size() > (variadic ? 32 : arity))
			rule_fail(at, "invalid expression arity");
		std::vector<TypedNode> types;
		RuleExpressionNode node;
		node.op = op;
		for (const auto &arg : args)
		{
			types.push_back(expression(arg, depth + 1));
			node.args.push_back(types.back().index);
		}
		Type type = Type::Integer;
		std::string unit = types.front().unit;
		const auto same = [&]()
		{
			for (const auto &t : types)
				if (t.type != types.front().type || t.unit != types.front().unit)
					rule_fail(at, "expression operands require matching types and units");
		};
		if (op == Op::All || op == Op::Any || op == Op::Not)
		{
			for (const auto &t : types)
				if (t.type != Type::Boolean)
					rule_fail(at, "Boolean operator needs Boolean operands");
			type = Type::Boolean;
			unit.clear();
		}
		else if (op == Op::Present)
		{
			type = Type::Boolean;
			unit.clear();
		}
		else if (op >= Op::Equal && op <= Op::GreaterEqual)
		{
			same();
			if (op != Op::Equal && op != Op::NotEqual && types.front().type != Type::Integer)
				rule_fail(at, "ordered comparison needs integers");
			type = Type::Boolean;
			unit.clear();
		}
		else
		{
			for (const auto &t : types)
				if (t.type != Type::Integer)
					rule_fail(at, "arithmetic needs integers");
			if (op == Op::Multiply)
			{
				if (!types[0].unit.empty() && !types[1].unit.empty())
					rule_fail(at, "multiply requires a scalar operand");
				unit = types[0].unit.empty() ? types[1].unit : types[0].unit;
			}
			else if (op == Op::Divide)
			{
				if (types[1].unit.empty())
					unit = types[0].unit;
				else if (types[0].unit == types[1].unit)
					unit.clear();
				else
					rule_fail(at, "division has incompatible units");
			}
			else if (op == Op::Ratio)
			{
				if (types[0].unit != types[1].unit)
					rule_fail(at, "ratio requires matching numerator and denominator units");
				unit = types[2].unit;
			}
			else
				same();
		}
		return append(std::move(node), type, std::move(unit));
	}

public:
	ExpressionCompiler(const Catalog &c, RuleDefinition &r, std::string path, SharedPropertyCatalog compiled = {})
	    : catalog(c), properties(compiled ? std::move(compiled) : compile_property_catalog(c)), rule(r),
	      at(std::move(path))
	{
	}
	RuleExpression compile(const Json &value)
	{
		const auto root = expression(value, 0);
		result.root = root.index;
		result.type = root.type;
		result.unit = root.unit;
		return std::move(result);
	}
};
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

RuleExpression compile_rule_expression(const Catalog &catalog, RuleDefinition &rule, const Json &value,
                                       const std::string &path, SharedPropertyCatalog compiled)
{
	return ExpressionCompiler(catalog, rule, path, std::move(compiled)).compile(value);
}
std::vector<RuleCostDefinition> compile_rule_costs(const Catalog &catalog, RuleDefinition &result, const Json &entries,
                                                   const std::string &at, const std::string &quantity,
                                                   SharedPropertyCatalog supplied)
{
	const auto properties = supplied ? std::move(supplied) : compile_property_catalog(catalog);
	const auto expression = [&](const Json &value, const std::string &field, Type type, const std::string &unit)
	{
		auto compiled = ExpressionCompiler(catalog, result, at + "." + field, properties).compile(value);
		if (compiled.type != type || compiled.unit != unit)
			rule_fail(at, "expression has incompatible type or units; expected " + unit);
		return compiled;
	};

	if (!entries.is_array() || entries.empty() || entries.size() > 32)
		rule_fail(at, "expected 1..32 costs");
	std::vector<RuleCostDefinition> compiled;
	for (const auto &entry : entries)
	{
		fields(entry, {"payer", "equipment", "store", "resource", "property", quantity}, at);
		RuleCostDefinition cost;
		cost.payer = role(entry.value("payer", std::string("target")), at);
		cost.equipment = entry.value("equipment", std::string{});
		cost.store = entry.value("store", std::string{});
		cost.resource = entry.value("resource", std::string{});
		cost.property = entry.value("property", std::string{});
		if ((!cost.equipment.empty() && cost.equipment != "$owner" && cost.equipment != "$source" &&
		     cost.equipment != "$target" && cost.equipment != "$hit" && !valid_identifier(cost.equipment)) ||
		    cost.store.size() > 256)
			rule_fail(at, "invalid cost equipment/store");
		std::string unit = "resource:" + cost.resource;
		if (!cost.property.empty())
		{
			const auto &definitions = properties;
			if (!cost.store.empty() || !cost.resource.empty() || !definitions->contains(cost.property) ||
			    definitions->at(cost.property).kind != CustomPropertyKind::Quantity)
				rule_fail(at, "property cost requires a quantity without store/resource");
			unit = definitions->at(cost.property).unit;
		}
		else if (cost.store.empty() || !catalog.resources.contains(cost.resource))
			rule_fail(at, "cost requires store and known resource");
		if (!entry.contains(quantity))
			rule_fail(at, "missing " + quantity);
		cost.amount = expression(entry.at(quantity), quantity, Type::Integer, unit);
		compiled.push_back(std::move(cost));
	}
	return compiled;
}
void compile_rule_values(const Catalog &catalog, RuleDefinition &result, const Json &overrides, const std::string &at,
                         SharedPropertyCatalog supplied)
{
	const auto properties = supplied ? std::move(supplied) : compile_property_catalog(catalog);
	const auto expression = [&](const Json &value, const std::string &field, Type type, const std::string &unit)
	{
		auto compiled = ExpressionCompiler(catalog, result, at + "." + field, properties).compile(value);
		if (compiled.type != type || compiled.unit != unit)
			rule_fail(at, "expression has incompatible type or units; expected " + unit);
		return compiled;
	};

	if (!overrides.is_object() || overrides.size() > 32)
		rule_fail(at, "values requires operation-index object");
	for (const auto &[index, over] : overrides.items())
	{
		if (index.empty() || index.size() > 2 || index.find_first_not_of("0123456789") != std::string::npos ||
		    (index.size() > 1 && index[0] == '0'))
			rule_fail(at, "invalid operation index");
		const auto i = static_cast<std::size_t>(std::stoul(index));
		if (i >= result.effects.operations.size())
			rule_fail(at, "operation index outside effect group");
		fields(over, {"damage", "amount", "delta_permille", "value"}, at + ".values." + index);
		RuleOperationExpressions compiled;
		const auto &operation = result.effects.operations[i];
		if (over.contains("damage"))
		{
			if (operation.kind != EffectOperationKind::Damage)
				rule_fail(at, "damage expression requires damage operation");
			compiled.damage = expression(over.at("damage"), "values." + index + ".damage", Type::Integer, "damage");
		}
		if (over.contains("amount"))
		{
			if (operation.kind == EffectOperationKind::Damage || operation.kind == EffectOperationKind::Apply ||
			    operation.kind == EffectOperationKind::Remove || operation.kind == EffectOperationKind::StoreFill ||
			    operation.kind == EffectOperationKind::PropertySet)
				rule_fail(at, "amount expression requires quantified store operation");
			compiled.amount = expression(
			    over.at("amount"), "values." + index + ".amount", Type::Integer,
			    (operation.kind == EffectOperationKind::HeatAdd || operation.kind == EffectOperationKind::HeatRemove)
			        ? "heat"
			    : operation.property.empty() ? "resource:" + operation.resource
			                                 : properties->at(operation.property).unit);
		}
		if (over.contains("value"))
		{
			if (operation.kind != EffectOperationKind::PropertySet)
				rule_fail(at, "value expression requires property_set");
			const auto &definitions = properties;
			const auto &d = definitions->at(operation.property);
			const auto type = d.kind == CustomPropertyKind::Flag   ? Type::Boolean
			                  : d.kind == CustomPropertyKind::Enum ? Type::Text
			                                                       : Type::Integer;
			compiled.value = expression(over.at("value"), "values." + index + ".value", type,
			                            d.kind == CustomPropertyKind::Enum ? "enum:" + d.id : d.unit);
		}
		if (over.contains("delta_permille"))
		{
			if (operation.kind != EffectOperationKind::Apply ||
			    catalog.effects.at(operation.effect).value("kind", std::string{}) != "modifier")
				rule_fail(at, "delta_permille expression requires modifier apply");
			compiled.delta_permille =
			    expression(over.at("delta_permille"), "values." + index + ".delta_permille", Type::Integer, "permille");
		}
		if (!compiled.damage && !compiled.amount && !compiled.delta_permille && !compiled.value)
			rule_fail(at, "empty operation expression override");
		result.values.emplace(i, std::move(compiled));
	}
}
RuleDefinition compile_rule(const Catalog &catalog, const std::string &id, SharedPropertyCatalog compiled)
try
{
	const auto properties = compiled ? std::move(compiled) : compile_property_catalog(catalog);
	const auto at = "effects." + id;
	const auto found = catalog.effects.find(id);
	if (found == catalog.effects.end() || found->second.value("kind", std::string{}) != "rule")
		rule_fail(at, "expected rule template");
	const auto &value = found->second;
	fields(value,
	       {"id", "kind", "name", "label", "extends", "trigger", "phase", "when", "effect", "values", "damage_permille",
	        "energy", "period_ticks", "costs", "funding"},
	       at);
	RuleDefinition result;
	result.id = id;
	result.label = value.value("label", id);
	if (result.label.empty() || result.label.size() > 512)
		rule_fail(at + ".label", "expected 1..512 characters");
	result.trigger = trigger(value.value("trigger", std::string("activate")), at);
	const auto phase = value.value("phase", std::string("instant"));
	if (phase == "instant")
		result.phase = RulePhase::Instant;
	else if (phase == "periodic")
		result.phase = RulePhase::Periodic;
	else if (phase == "maintained")
		result.phase = RulePhase::Maintained;
	else
		rule_fail(at, "unknown phase " + phase);
	result.period_ticks = integer(value.value("period_ticks", Json(1)), 1, 1000000, at + ".period_ticks");
	if (result.phase != RulePhase::Periodic && value.contains("period_ticks"))
		rule_fail(at, "period_ticks requires periodic phase");
	if (result.phase == RulePhase::Maintained && result.trigger != RuleTrigger::Maintain)
		rule_fail(at, "maintained phase requires maintain trigger");
	if (result.trigger == RuleTrigger::Maintain && result.phase != RulePhase::Maintained)
		rule_fail(at, "maintain trigger requires maintained phase");
	const bool clock_trigger = result.trigger == RuleTrigger::Tick || result.trigger == RuleTrigger::Occupancy;
	if (clock_trigger != (result.phase == RulePhase::Periodic))
		rule_fail(at, "tick/occupancy triggers require periodic phase; other triggers use instant or maintained phase");
	const auto effect = value.value("effect", std::string{});
	if (!effect.empty())
		result.effects = compile_effect_group(catalog, effect, properties);
	if (effect.empty() && !value.contains("damage_permille"))
		rule_fail(at, "rule needs an effect or pending damage transformation");
	const auto expression = [&](const Json &v, const std::string &field, Type type, const std::string &unit)
	{
		auto compiled = ExpressionCompiler(catalog, result, at + "." + field, properties).compile(v);
		if (compiled.type != type || compiled.unit != unit)
			rule_fail(at + "." + field, "expression has incompatible type or units; expected " + unit);
		return compiled;
	};
	const auto costs = [&](const Json &entries, const std::string &field, const std::string &quantity)
	{
		return compile_rule_costs(catalog, result, entries, at + "." + field, quantity, properties);
	};
	if (value.contains("costs"))
		result.costs = costs(value.at("costs"), "costs", "amount");
	if (result.costs.size() + result.effects.costs.size() > 32)
		rule_fail(at, "combined entry costs exceed 32");
	if (value.contains("when"))
		result.condition = expression(value.at("when"), "when", Type::Boolean, {});
	if (value.contains("damage_permille"))
	{
		if (!pre_damage(result.trigger))
			rule_fail(at, "damage_permille requires a pre-damage trigger");
		result.damage_permille = expression(value.at("damage_permille"), "damage_permille", Type::Integer, "permille");
	}
	if (value.contains("energy"))
	{
		if (result.effects.payer.empty())
			rule_fail(at, "energy expression requires an explicit effect payer");
		result.energy = expression(value.at("energy"), "energy", Type::Integer, "energy");
	}
	if (value.contains("values"))
		compile_rule_values(catalog, result, value.at("values"), at, properties);
	if (result.phase == RulePhase::Maintained)
		for (const auto &op : result.effects.operations)
			if (op.kind != EffectOperationKind::Apply)
				rule_fail(at, "maintained rules only own apply contributions; instant consequences need an instant or "
				              "periodic rule");
	result.scalable = result.phase == RulePhase::Maintained && !result.effects.operations.empty() &&
	                  std::all_of(result.effects.operations.begin(), result.effects.operations.end(),
	                              [&](const auto &op)
	                              {
		                              return op.kind == EffectOperationKind::Apply &&
		                                     catalog.effects.at(op.effect).value("kind", std::string{}) == "modifier";
	                              });
	if (value.contains("funding"))
	{
		if (result.phase == RulePhase::Instant)
			rule_fail(at, "ongoing funding requires maintained or periodic phase");
		const auto &f = value.at("funding");
		fields(f, {"shortage", "costs"}, at + ".funding");
		RuleFundingDefinition funding;
		funding.costs = costs(f.at("costs"), "funding.costs", "per_second");
		funding.shortage = f.value("shortage", std::string("pause"));
		if (funding.shortage != "pause" && funding.shortage != "stop" && funding.shortage != "proportional")
			rule_fail(at, "unknown shortage policy");
		if (funding.shortage == "proportional" && !result.scalable)
			rule_fail(at, "proportional funding requires maintained modifier applies only");
		result.funding = std::move(funding);
	}
	return result;
}
catch (const Json::exception &error)
{
	rule_fail("effects." + id, std::string("invalid rule authoring: ") + error.what());
}
RulesCatalog compile_rule_catalog(const Catalog &catalog, SharedPropertyCatalog compiled)
{
	const auto properties = compiled ? std::move(compiled) : compile_property_catalog(catalog);
	RulesCatalog result;
	for (const auto &[id, value] : catalog.effects)
		if (value.value("kind", std::string{}) == "rule")
			result.emplace(id, compile_rule(catalog, id, properties));
	return result;
}
RulesCatalog compile_bound_rules(const Catalog &catalog, const Json &source, SharedPropertyCatalog compiled)
{
	const auto properties = compiled ? std::move(compiled) : compile_property_catalog(catalog);
	RulesCatalog result;
	std::vector<std::pair<const Json *, std::size_t>> pending{{&source, 0}};
	std::size_t visited = 0;
	while (!pending.empty())
	{
		const auto [value, depth] = pending.back();
		pending.pop_back();
		if (depth > 64 || ++visited > 1000000)
			rule_fail("rules", "source definition exceeds bounded compilation scan");
		if (value->is_object())
		{
			if (value->contains("rules"))
			{
				const auto &bindings = value->at("rules");
				if (!bindings.is_array())
					rule_fail("rules", "expected binding array");
				for (const auto &binding : bindings)
				{
					if (!binding.is_object() || !binding.contains("rule") || !binding.at("rule").is_string())
						rule_fail("rules", "expected rule reference");
					const auto id = binding.at("rule").get<std::string>();
					if (!result.contains(id))
						result.emplace(id, compile_rule(catalog, id, properties));
				}
			}
			for (const auto &[key, item] : value->items())
				if (key != "rules" && (item.is_object() || item.is_array()))
					pending.push_back({&item, depth + 1});
		}
		else if (value->is_array())
			for (const auto &item : *value)
				if (item.is_object() || item.is_array())
					pending.push_back({&item, depth + 1});
	}
	return result;
}
RulesCatalog compile_entity_rule_scope(const Catalog &catalog, const std::string &entity,
                                       SharedPropertyCatalog compiled)
{
	const auto properties = compiled ? std::move(compiled) : compile_property_catalog(catalog);
	const auto &definition = catalog.entities.at(entity);
	auto result = compile_bound_rules(catalog, definition, properties);
	if (definition.contains("systems"))
		for (const auto &[id, system] : definition.at("systems").items())
			if (system.contains("shield_effect") && system.at("shield_effect").is_string())
			{
				const auto effect = system.at("shield_effect").get<std::string>();
				const auto found = catalog.effects.find(effect);
				if (found != catalog.effects.end())
				{
					auto bindings = compile_bound_rules(catalog, found->second, properties);
					result.merge(bindings);
				}
			}
	return result;
}
std::vector<RuleBinding> compile_rule_bindings(const RulesCatalog &catalog, const Json &value, const std::string &at,
                                               std::span<const RuleTrigger> allowed)
try
{
	if (!value.is_array() || value.size() > rule_binding_limit)
		rule_fail(at, "expected at most 64 rule bindings");
	std::set<std::string> ids;
	std::vector<RuleBinding> result;
	for (const auto &entry : value)
	{
		fields(entry, {"id", "rule", "target", "equipment", "officer", "tags", "eligibility"}, at);
		RuleBinding binding;
		binding.id = entry.value("id", std::string{});
		binding.rule = entry.value("rule", std::string{});
		const auto eligibility = entry.value("eligibility", std::string("duty"));
		if (eligibility != "duty" && eligibility != "lifetime")
			rule_fail(at, "unknown source eligibility");
		binding.lifetime = eligibility == "lifetime";
		if (binding.lifetime && at.find(".personnel.officers.") == std::string::npos)
			rule_fail(at, "lifetime eligibility requires an explicit officer owner binding");
		if (!valid_identifier(binding.id) || !ids.insert(binding.id).second || !catalog.contains(binding.rule))
			rule_fail(at, "invalid/duplicate binding or unknown rule");
		binding.target = role(entry.value("target", std::string("subject")), at);
		binding.equipment = entry.value("equipment", std::string{});
		binding.officer = entry.value("officer", std::string{});
		if ((!binding.equipment.empty() && !valid_identifier(binding.equipment)) ||
		    (!binding.officer.empty() && !valid_identifier(binding.officer)) ||
		    (!binding.equipment.empty() && !binding.officer.empty()))
			rule_fail(at, "binding requires at most one valid equipment or officer target");
		if (entry.contains("tags"))
		{
			const auto &tags = entry.at("tags");
			if (!tags.is_array() || tags.size() > 32)
				rule_fail(at, "expected at most 32 required tags");
			std::set<std::string> unique;
			for (const auto &tag : tags)
			{
				if (!tag.is_string() || tag.get<std::string>().empty() || tag.get<std::string>().size() > 128 ||
				    !unique.insert(tag.get<std::string>()).second)
					rule_fail(at, "invalid or duplicate required tag");
				binding.tags.push_back(tag.get<std::string>());
			}
		}
		if (!allowed.empty() &&
		    std::find(allowed.begin(), allowed.end(), catalog.at(binding.rule).trigger) == allowed.end())
			rule_fail(at, "rule trigger unsupported by this source");
		result.push_back(std::move(binding));
	}
	return result;
}
catch (const Json::exception &error)
{
	rule_fail(at, std::string("invalid rule binding: ") + error.what());
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
SourceRuleDefinitionSet compile_source_rules(const Catalog &catalog, const std::string &entity)
{
	const auto rules = compile_entity_rule_scope(catalog, entity);
	return compile_source_rules(catalog, entity, rules);
}
SourceRuleDefinitionSet compile_source_rules(const Catalog &catalog, const std::string &entity,
                                             const RulesCatalog &rules)
{
	SourceRuleDefinitionSet result;
	const auto &definition = catalog.entities.at(entity);
	static constexpr std::array allowed{RuleTrigger::Activate,
	                                    RuleTrigger::Deactivate,
	                                    RuleTrigger::Tick,
	                                    RuleTrigger::Maintain,
	                                    RuleTrigger::Destroy,
	                                    RuleTrigger::PreShieldDamage,
	                                    RuleTrigger::PostShieldDamage,
	                                    RuleTrigger::PreCompartmentDamage,
	                                    RuleTrigger::PostCompartmentDamage,
	                                    RuleTrigger::PreSystemDamage,
	                                    RuleTrigger::PostSystemDamage,
	                                    RuleTrigger::PostEntityDamage,
	                                    RuleTrigger::PropertyChanged};
	result.entity = compile_rule_bindings(rules, definition.value("rules", Json::array()),
	                                      "entities." + entity + ".rules", allowed);
	if (definition.contains("systems"))
		for (const auto &[id, system] : definition.at("systems").items())
			if (system.contains("rules"))
				result.equipment.emplace(id, compile_rule_bindings(rules, system.at("rules"),
				                                                   "entities." + entity + ".systems." + id + ".rules",
				                                                   allowed));
	if (definition.contains("personnel") && !definition.at("personnel").is_null())
		for (const auto &officer : definition.at("personnel").at("officers"))
			if (officer.contains("rules"))
			{
				const auto id = officer.at("id").get<std::string>();
				auto bindings = compile_rule_bindings(
				    rules, officer.at("rules"), "entities." + entity + ".personnel.officers." + id + ".rules", allowed);
				for (auto &b : bindings)
					(b.lifetime ? result.officer_lifetime[id] : result.officers[id]).push_back(std::move(b));
			}
	return result;
}
std::map<std::string, std::vector<RuleBinding>> compile_effect_rules(const Catalog &catalog, const RulesCatalog &rules)
{
	std::map<std::string, std::vector<RuleBinding>> result;
	static constexpr std::array allowed{RuleTrigger::Activate,
	                                    RuleTrigger::Deactivate,
	                                    RuleTrigger::Tick,
	                                    RuleTrigger::Maintain,
	                                    RuleTrigger::Destroy,
	                                    RuleTrigger::PreShieldDamage,
	                                    RuleTrigger::PostShieldDamage,
	                                    RuleTrigger::PreCompartmentDamage,
	                                    RuleTrigger::PostCompartmentDamage,
	                                    RuleTrigger::PreSystemDamage,
	                                    RuleTrigger::PostSystemDamage,
	                                    RuleTrigger::PostEntityDamage,
	                                    RuleTrigger::PropertyChanged};
	for (const auto &[id, effect] : catalog.effects)
		if (effect.contains("rules"))
		{
			const auto kind = effect.value("kind", std::string{});
			if (kind != "shield" && kind != "modifier" && kind != "periodic" && kind != "ability_grant" &&
			    kind != "payload" && kind != "process" && kind != "process_grant")
				rule_fail("effects." + id + ".rules", "this template has no native binding owner");
			// Payload/periodic adapters already own their binding list and native cadence.
			if (kind == "payload" || kind == "periodic")
				continue;
			result.emplace(id, compile_rule_bindings(rules, effect.at("rules"), "effects." + id + ".rules", allowed));
		}
	return result;
}
std::map<std::string, std::vector<RuleBinding>> compile_region_rules(const Catalog &, const Json &map,
                                                                     const RulesCatalog &rules)
{
	std::map<std::string, std::vector<RuleBinding>> result;
	const auto &source = map.contains("scenario") ? map.at("scenario") : map;
	const auto regions = map.contains("regions") ? map.at("regions") : source.value("regions", Json::array());
	const auto at = "maps." + map.value("id", std::string("map")) + ".regions";
	if (!regions.is_array() || regions.size() > 256)
		rule_fail(at, "expected at most 256 regions");
	for (const auto &region : regions)
	{
		if (!region.is_object())
			rule_fail(at, "expected region object");
		const auto id = region.value("id", std::string{});
		if (!valid_identifier(id) || result.contains(id))
			rule_fail(at, "invalid or duplicate region identity");
		auto bindings = compile_rule_bindings(rules, region.value("rules", Json::array()), at + "." + id + ".rules");
		for (const auto &binding : bindings)
		{
			const auto &rule = rules.at(binding.rule);
			if ((rule.trigger == RuleTrigger::Enter || rule.trigger == RuleTrigger::Exit) &&
			    rule.phase != RulePhase::Instant)
				rule_fail(at + "." + id, "entry/exit rules require instant phase");
			if (rule.trigger == RuleTrigger::Occupancy && rule.phase != RulePhase::Periodic)
				rule_fail(at + "." + id, "occupancy rules require periodic phase");
		}
		result.emplace(id, std::move(bindings));
	}
	return result;
}
} // namespace phoenix
