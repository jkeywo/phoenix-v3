#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
import phoenix.data;
import phoenix.resources;
extern "C" void stock_accounting_contracts()
{
	using namespace phoenix;
	const auto require = [](bool value, const char *message)
	{
		if (!value)
			throw std::runtime_error(message);
	};
	const ResourceCatalogue resources{
	    {"round", {"round", "Round", {"ammo", "cargo"}, {{"magazine", 1}, {"cargo", 3}}, {}}},
	    {"plasma", {"plasma", "Plasma", {"ammo", "cargo"}, {{"magazine", 1}, {"cargo", 4}}, {}}},
	    {"fuel", {"fuel", "Fuel", {"cargo"}, {{"cargo", 2}}, {}}}};
	StockStoreDefinition mixed_definition;
	mixed_definition.id = "hold";
	mixed_definition.capacity_tag = "cargo";
	mixed_definition.capacity = 20;
	mixed_definition.filter.tags = {"cargo"};
	mixed_definition.initial = {{"round", 2}, {"fuel", 3}};
	const auto shared_resources = share_resource_catalogue(resources);
	StockStore hold(mixed_definition, shared_resources);
	{
		StockStore peer(mixed_definition, shared_resources);
		auto copy = hold;
		require(&hold.catalogue() == &peer.catalogue() && &hold.catalogue() == &copy.catalogue() &&
		            &hold.definition() == &copy.definition(),
		        "stores and staged copies share immutable catalogue and definition bindings");
		require(copy.consume("fuel", 1) && hold.quantity("fuel") == 3 && peer.quantity("fuel") == 3,
		        "shared immutable bindings do not share mutable stock quantities");
	}
	require(hold.occupied() == 12 && hold.headroom("round") == 2,
	        "mixed stock uses the selected per-tag capacity measure");
	require(hold.reserve_capacity("delivery:1", "round", 2) && hold.headroom("fuel") == 1,
	        "receiving claims share capacity with every resource already present");
	const auto reserved = hold.save();
	require(!hold.add("fuel", 2) && hold.save() == reserved,
	        "failed exact receipt cannot clip or disturb a capacity commitment");
	require(hold.claim_stock("job:1", "fuel", 2) && hold.available("fuel") == 1,
	        "stock claims reduce spendable stock without counting as receiving space");
	require(!hold.set("fuel", 1) && hold.quantity("fuel") == 3,
	        "set cannot destroy stock claimed by another operation");
	require(hold.fill("fuel") == 1 && hold.quantity("round") == 2 && hold.headroom("fuel") == 0,
	        "fill uses only uncommitted compatible space and preserves other resources");
	require(hold.receive_reserved("delivery:1") && !hold.receive_reserved("delivery:1") && hold.occupied() == 20,
	        "a reserved receipt deposits exactly once");
	require(hold.consume_claim("job:1") && !hold.consume_claim("job:1") && hold.quantity("fuel") == 2,
	        "a finite stock claim consumes once without refunding completed work");

	StockStoreDefinition magazine_definition;
	magazine_definition.id = "magazine";
	magazine_definition.capacity_tag = "magazine";
	magazine_definition.capacity = 5;
	magazine_definition.filter.tags = {"ammo"};
	magazine_definition.single_resource = true;
	magazine_definition.selection = "round";
	magazine_definition.initial = {{"round", 3}};
	StockStore magazine(magazine_definition, resources);
	require(magazine.consume("round", 3) && magazine.selected() == "round" && !magazine.add("plasma", 1),
	        "empty single-resource stores retain selection rather than substituting resources");
	require(magazine.reserve_capacity("weapon:loaded", "round", 1) && !magazine.select("plasma"),
	        "loaded return-space claims prevent changing an empty magazine's resource");
	require(magazine.release_capacity("weapon:loaded") && magazine.select("plasma") && magazine.add("plasma", 1),
	        "selection can change only when empty and free of commitments");
	const auto before_hold = hold.save(), before_magazine = magazine.save();
	StockTransaction failed;
	require(!failed.transfer(hold, magazine, "round", 1) && !failed.commit() && hold.save() == before_hold &&
	            magazine.save() == before_magazine,
	        "incompatible transfer rolls back its source debit and every staged store");
	StockTransaction aliases;
	require(aliases.consume(hold, "fuel", 1) && !aliases.consume(hold, "fuel", 2) && !aliases.commit() &&
	            hold.save() == before_hold,
	        "repeated references share one prepared balance and cannot spend untouched stock twice");
	StockTransaction pending;
	require(pending.consume(hold, "fuel", 1) && hold.consume("fuel", 1) && !pending.commit(),
	        "prepared operations reject a concurrently changed live store");
	auto restored = hold;
	restored.restore(before_hold);
	require(restored.save() == before_hold, "stock and claims continue exactly after restore");
	StockTransaction stale_restore;
	require(stale_restore.consume(restored, "fuel", 1), "prepare restore conflict");
	auto alternate = before_hold;
	alternate["quantities"]["fuel"] = 1;
	restored.restore(alternate);
	require(!stale_restore.commit(), "restoring different contents at the same saved revision invalidates preparation");
	StockTransaction rebound;
	require(rebound.consume(restored, "fuel", 1), "prepare binding replacement conflict");
	auto replacement_definition = restored.definition();
	replacement_definition.capacity += 1;
	StockStore replacement(replacement_definition, resources);
	replacement.restore(restored.save());
	restored = replacement;
	require(!rebound.commit(), "replacing the store definition at the same revision invalidates preparation");
	auto malformed = before_hold;
	malformed["capacity_claims"]["overflow"] = {{"resource", "round"}, {"quantity", 100}};
	const auto before_failure = restored.save();
	bool rejected = false;
	try
	{
		restored.restore(malformed);
	}
	catch (const std::exception &)
	{
		rejected = true;
	}
	require(rejected && restored.save() == before_failure,
	        "restore validates all capacity claims before replacing live stock");
	malformed = before_hold;
	malformed["quantities"]["unknown"] = 1;
	rejected = false;
	try
	{
		restored.restore(malformed);
	}
	catch (const std::exception &)
	{
		rejected = true;
	}
	require(rejected && restored.save() == before_failure, "restore rejects unknown catalogue identities atomically");

	auto conjunction = mixed_definition;
	conjunction.filter.tags = {"ammo", "cargo"};
	conjunction.filter.resources = {"round"};
	conjunction.filter.require_all = true;
	conjunction.initial.clear();
	StockStore restricted(conjunction, resources);
	require(restricted.accepts("round") && !restricted.accepts("plasma") && !restricted.accepts("fuel"),
	        "AND filters require authored tags and exact-resource membership");
	conjunction.filter.require_all = false;
	StockStore alternatives(conjunction, resources);
	require(alternatives.accepts("round") && alternatives.accepts("plasma") && alternatives.accepts("fuel"),
	        "OR filters accept an exact identity or a matching tag");
	auto too_large = mixed_definition;
	too_large.capacity = resource_limit;
	too_large.initial = {{"round", resource_limit}};
	rejected = false;
	try
	{
		StockStore invalid(too_large, resources);
	}
	catch (const std::exception &)
	{
		rejected = true;
	}
	require(rejected, "capacity products are checked before quantity initialization");
}
