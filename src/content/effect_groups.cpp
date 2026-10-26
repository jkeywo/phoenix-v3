module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
module phoenix.content;

namespace phoenix
{
EffectGroupDefinition compile_effect_operations(const Catalog &catalog, const Json &group, const std::string &at)
{
	const auto fail = [&](const std::string &reason)
	{
		throw std::runtime_error(at + ": " + reason);
	};
	if (!group.is_object())
		fail("expected operations object");
	for (const auto &[key, value] : group.items())
		if (key != "operations" && key != "payer" && key != "energy" && key != "costs")
			fail("unsupported field " + key);
	EffectGroupDefinition result;
	result.payer = group.value("payer", std::string{});
	const auto energy = group.value("energy", Json(0));
	if (!energy.is_number_integer() || energy < 0 || energy > 1000000000)
		fail("invalid energy cost");
	result.energy = energy.get<std::int64_t>();
	if (result.energy && !valid_identifier(result.payer))
		fail("paid group requires equipment payer");
	if (group.contains("costs"))
	{
		const auto &costs = group.at("costs");
		if (!costs.is_array() || costs.empty() || costs.size() > 32)
			fail("expected 1..32 explicit costs");
		for (const auto &entry : costs)
		{
			if (!entry.is_object())
				fail("expected cost object");
			for (const auto &[key, unused] : entry.items())
				if (key != "payer" && key != "equipment" && key != "store" && key != "resource" && key != "amount")
					fail("unsupported cost field " + key);
			EffectCostDefinition cost;
			cost.payer = entry.value("payer", std::string("target"));
			if (cost.payer != "owner" && cost.payer != "source" && cost.payer != "instigator" &&
			    cost.payer != "subject" && cost.payer != "target" && cost.payer != "protected" &&
			    cost.payer != "attacker")
				fail("cost requires supported payer role");
			cost.equipment = entry.value("equipment", std::string{});
			cost.store = entry.value("store", std::string{});
			cost.resource = entry.value("resource", std::string{});
			if ((!cost.equipment.empty() && cost.equipment != "$owner" && cost.equipment != "$source" &&
			     cost.equipment != "$target" && cost.equipment != "$hit" && !valid_identifier(cost.equipment)) ||
			    cost.store.empty() || cost.store.size() > 256 || !catalog.resources.contains(cost.resource))
				fail("invalid cost equipment/store/resource");
			const auto amount = entry.value("amount", Json(-1));
			if (!amount.is_number_integer() || amount < 0 || amount > resource_limit)
				fail("invalid cost amount");
			cost.amount = amount.get<std::int64_t>();
			result.costs.push_back(std::move(cost));
		}
	}
	if (!group.contains("operations") || !group.at("operations").is_array() || group.at("operations").empty() ||
	    group.at("operations").size() > 32)
		fail("expected 1..32 operations");
	bool damage_seen = false;
	for (const auto &value : group.at("operations"))
	{
		if (!value.is_object())
			fail("expected operation object");
		EffectOperation op;
		op.target = value.value("target", std::string("attacker"));
		if (op.target != "protected" && op.target != "attacker" && op.target != "owner" && op.target != "source" &&
		    op.target != "instigator" && op.target != "subject" && op.target != "target")
			fail("target must name a supported context role");
		const auto kind = value.value("kind", std::string{});
		std::set<std::string> fields{"kind", "target"};
		if (kind == "damage")
		{
			fields.insert("damage");
			op.kind = EffectOperationKind::Damage;
			const auto amount = value.value("damage", Json(0));
			if (!amount.is_number_integer() || amount <= 0 || amount > 1000000)
				fail("invalid damage");
			op.damage = amount.get<std::int64_t>();
			damage_seen = true;
		}
		else
		{
			if (damage_seen)
				fail("prepared operations must precede damage");
			if (kind == "apply" || kind == "remove")
			{
				fields.insert("grant");
				fields.insert("equipment");
				fields.insert("officer");
				fields.insert("delta_permille");
				fields.insert("role");
				op.kind = kind == "apply" ? EffectOperationKind::Apply : EffectOperationKind::Remove;
				if (kind == "remove")
				{
					fields.insert("scope");
					op.scope = value.value("scope", std::string("local"));
					if (op.scope != "local" && op.scope != "owner")
						fail("remove scope must be local or owner");
					if (op.scope == "owner")
					{
						if (!value.contains("target"))
							op.target = "owner";
						if (op.target != "owner")
							fail("owner-scoped removal requires owner target");
						for (const auto *field :
						     {"grant", "effect", "equipment", "officer", "all_sources", "delta_permille", "role"})
							if (value.contains(field))
								fail("owner-scoped removal omits named grant/effect/source filters");
					}
					fields.insert("all_sources");
					op.all_sources = value.value("all_sources", false);
					if (op.all_sources)
					{
						fields.insert("effect");
						op.effect = value.value("effect", std::string{});
						if (!catalog.effects.contains(op.effect) ||
						    catalog.effects.at(op.effect).value("kind", std::string{}) != "periodic")
							fail("all_sources removal requires a periodic effect definition");
					}
				}
				op.grant = value.value("grant", std::string{});
				op.equipment = value.value("equipment", std::string{});
				op.officer = value.value("officer", std::string{});
				if ((!op.officer.empty() && !valid_identifier(op.officer)) ||
				    (!op.officer.empty() && !op.equipment.empty()))
					fail("ability source must bind one valid officer or equipment");
				if (op.all_sources && (!op.equipment.empty() || !op.officer.empty()))
					fail("all_sources removal targets the entity, not equipment");
				if ((!op.all_sources && op.scope != "owner" && !valid_identifier(op.grant)) ||
				    (!op.equipment.empty() && op.equipment != "$hit" && op.equipment != "$owner" &&
				     op.equipment != "$source" && op.equipment != "$target" && !valid_identifier(op.equipment)))
					fail("invalid grant or equipment binding");
				if (kind == "apply")
				{
					fields.insert("effect");
					op.effect = value.value("effect", std::string{});
					if (!catalog.effects.contains(op.effect))
						fail("unknown effect " + op.effect);
					const auto effect_kind = catalog.effects.at(op.effect).value("kind", std::string{});
					op.role = value.value("role", std::string{});
					if (value.contains("role") &&
					    (effect_kind != "modifier" ||
					     (!op.role.empty() && op.role != "$role" && !valid_identifier(op.role))))
						fail("role requires a modifier apply and a valid role or $role");
					if (!op.role.empty() && op.role != "$role")
					{
						const auto modifier = compile_modifier_effect(catalog, op.effect);
						bool found = false;
						for (const auto &contribution : modifier.contributions)
							found = found || contribution.role == op.role;
						if (!found)
							fail("modifier role selects no contribution");
					}
					if (value.contains("delta_permille"))
					{
						const auto &delta = value.at("delta_permille");
						if (effect_kind != "modifier" || !delta.is_number_integer() || delta < -100000 ||
						    delta > 100000)
							fail("delta_permille requires a modifier apply and integer in -100000..100000");
						op.delta_permille = delta.get<std::int64_t>();
					}
					if (effect_kind != "shield" && effect_kind != "modifier" && effect_kind != "periodic" &&
					    effect_kind != "ability_grant")
						fail("nested operation groups are unsupported");
					if (effect_kind == "ability_grant")
					{
						if (op.equipment.empty() && op.officer.empty())
							fail("ability grant requires an officer or equipment source binding");
						(void)compile_ability_grant(catalog, op.effect);
					}
					else if (!op.officer.empty() || (effect_kind == "modifier") != !op.equipment.empty())
						fail("modifier needs equipment; shield and periodic omit source bindings");
				}
				else if (value.contains("delta_permille") || value.contains("role"))
					fail("delta_permille and role require a modifier apply");
			}
			else
			{
				fields.insert("store");
				fields.insert("resource");
				if (kind == "add")
					op.kind = EffectOperationKind::StoreAdd;
				else if (kind == "consume")
					op.kind = EffectOperationKind::StoreConsume;
				else if (kind == "set")
					op.kind = EffectOperationKind::StoreSet;
				else if (kind == "fill")
					op.kind = EffectOperationKind::StoreFill;
				else if (kind == "transfer")
					op.kind = EffectOperationKind::StoreTransfer;
				else
					fail("unsupported operation " + kind);
				op.store = value.value("store", std::string{});
				op.resource = value.value("resource", std::string{});
				if (op.store.empty() || op.store.size() > 256 || !catalog.resources.contains(op.resource))
					fail("unknown resource or invalid store");
				if (kind != "fill")
				{
					fields.insert("amount");
					const auto amount = value.value("amount", Json(-1));
					if (!amount.is_number_integer() || amount < 0 || amount > resource_limit)
						fail("invalid exact amount");
					op.amount = amount.get<std::int64_t>();
				}
				if (kind == "transfer")
				{
					fields.insert("from_store");
					op.from_store = value.value("from_store", std::string{});
					if (op.from_store.empty() || op.from_store.size() > 256)
						fail("transfer requires source store");
				}
			}
		}
		for (const auto &[key, unused] : value.items())
			if (!fields.contains(key))
				fail("unsupported " + kind + " field " + key);
		result.operations.push_back(std::move(op));
	}
	return result;
}
EffectGroupDefinition compile_effect_group(const Catalog &catalog, const std::string &id)
{
	const auto found = catalog.effects.find(id);
	if (found == catalog.effects.end())
		throw std::runtime_error("effects." + id + ": unknown operations effect reference");
	const auto &definition = found->second;
	if (definition.value("kind", std::string{}) != "operations")
		throw std::runtime_error(id + ": expected operations effect");
	Json group = definition;
	for (const auto &key : {"id", "kind", "name", "label", "extends", "override"})
		group.erase(key);
	if (group.contains("operations") && group.at("operations").is_array())
		for (auto &operation : group["operations"])
			if (operation.is_object() && !operation.contains("target"))
				operation["target"] = operation.value("scope", std::string("local")) == "owner" ? "owner" : "protected";
	return compile_effect_operations(catalog, group, id);
}
} // namespace phoenix
