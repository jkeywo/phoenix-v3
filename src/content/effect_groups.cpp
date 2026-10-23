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
ShieldReactionDefinition compile_effect_operations(const Catalog &catalog, const Json &group, const std::string &at)
{
	const auto fail = [&](const std::string &reason)
	{
		throw std::runtime_error(at + ": " + reason);
	};
	if (!group.is_object())
		fail("expected operations object");
	for (const auto &[key, value] : group.items())
		if (key != "operations" && key != "payer" && key != "energy")
			fail("unsupported field " + key);
	ShieldReactionDefinition result;
	result.payer = group.value("payer", std::string{});
	const auto energy = group.value("energy", Json(0));
	if (!energy.is_number_integer() || energy < 0 || energy > 1000000000)
		fail("invalid energy cost");
	result.energy = energy.get<std::int64_t>();
	if (result.energy && !valid_identifier(result.payer))
		fail("paid group requires equipment payer");
	if (!group.contains("operations") || !group.at("operations").is_array() || group.at("operations").empty() ||
	    group.at("operations").size() > 32)
		fail("expected 1..32 operations");
	bool damage_seen = false;
	for (const auto &value : group.at("operations"))
	{
		if (!value.is_object())
			fail("expected operation object");
		ShieldReactionOperation op;
		op.target = value.value("target", std::string("attacker"));
		if (op.target != "protected" && op.target != "attacker")
			fail("target must be protected or attacker");
		const auto kind = value.value("kind", std::string{});
		std::set<std::string> fields{"kind", "target"};
		if (kind == "damage")
		{
			fields.insert("damage");
			op.kind = ShieldReactionKind::Damage;
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
				op.kind = kind == "apply" ? ShieldReactionKind::Apply : ShieldReactionKind::Remove;
				op.grant = value.value("grant", std::string{});
				op.equipment = value.value("equipment", std::string{});
				if (!valid_identifier(op.grant) || (!op.equipment.empty() && !valid_identifier(op.equipment)))
					fail("invalid grant or equipment binding");
				if (kind == "apply")
				{
					fields.insert("effect");
					op.effect = value.value("effect", std::string{});
					if (!catalog.effects.contains(op.effect))
						fail("unknown effect " + op.effect);
					const auto effect_kind = catalog.effects.at(op.effect).value("kind", std::string{});
					if (effect_kind != "shield" && effect_kind != "modifier")
						fail("nested operation groups are unsupported");
					if ((effect_kind == "modifier") != !op.equipment.empty())
						fail("modifier needs equipment; shield omits it");
				}
			}
			else
			{
				fields.insert("store");
				fields.insert("resource");
				if (kind == "add")
					op.kind = ShieldReactionKind::StoreAdd;
				else if (kind == "consume")
					op.kind = ShieldReactionKind::StoreConsume;
				else if (kind == "set")
					op.kind = ShieldReactionKind::StoreSet;
				else if (kind == "fill")
					op.kind = ShieldReactionKind::StoreFill;
				else if (kind == "transfer")
					op.kind = ShieldReactionKind::StoreTransfer;
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
ShieldReactionDefinition compile_effect_group(const Catalog &catalog, const std::string &id)
{
	const auto &definition = catalog.effects.at(id);
	if (definition.value("kind", std::string{}) != "operations")
		throw std::runtime_error(id + ": expected operations effect");
	Json group = definition;
	for (const auto &key : {"id", "kind", "name", "label", "extends", "override"})
		group.erase(key);
	if (group.contains("operations") && group.at("operations").is_array())
		for (auto &operation : group["operations"])
			if (operation.is_object() && !operation.contains("target"))
				operation["target"] = "protected";
	return compile_effect_operations(catalog, group, id);
}
} // namespace phoenix
