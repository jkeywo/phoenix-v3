module;
#include <algorithm>
#include <cstdint>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.content;
namespace phoenix
{
namespace
{
void stock_require(bool ok, const std::string &path, const std::string &why)
{
	if (!ok)
		throw std::runtime_error(path + ": " + why);
}
std::set<std::string> stock_names(const Json &values, const std::string &path, std::size_t maximum = 64)
{
	stock_require(values.is_array() && values.size() <= maximum, path, "expected bounded identifier array");
	std::set<std::string> result;
	for (const auto &value : values)
	{
		stock_require(value.is_string(), path, "expected identifier");
		const auto id = value.get<std::string>();
		stock_require(valid_identifier(id) && result.insert(id).second, path, "invalid or duplicate identifier");
	}
	return result;
}
std::int64_t stock_number(const Json &value, const std::string &path, std::int64_t low = 0,
                          std::int64_t high = resource_limit)
{
	stock_require(value.is_number_integer() && value >= low && value <= high, path, "integer outside supported bounds");
	return value.get<std::int64_t>();
}
void stock_fields(const Json &value, const std::set<std::string> &allowed, const std::string &path)
{
	stock_require(value.is_object(), path, "expected object");
	for (const auto &[key, child] : value.items())
		stock_require(allowed.contains(key), path + "." + key, "unsupported field");
}
} // namespace
ResourceCatalogue compile_resource_catalog(const Catalog &catalog)
{
	ResourceCatalogue result;
	stock_require(catalog.resources.size() <= 4096, "resources", "too many resource definitions");
	for (const auto &[id, value] : catalog.resources)
	{
		const auto path = "resources." + id;
		stock_require(valid_identifier(id), path, "invalid resource identity");
		stock_fields(value, {"id", "extends", "label", "tags", "sizes", "effects", "ammunition"}, path);
		ResourceDefinition resource;
		resource.id = id;
		resource.label = value.at("label").get<std::string>();
		stock_require(!resource.label.empty() && resource.label.size() <= 128, path, "invalid label");
		resource.tags = stock_names(value.at("tags"), path + ".tags");
		stock_require(!resource.tags.empty(), path, "resource requires tags");
		const auto &sizes = value.at("sizes");
		stock_require(sizes.is_object() && !sizes.empty() && sizes.size() <= 64, path, "expected capacity sizes");
		for (const auto &[tag, size] : sizes.items())
		{
			stock_require(resource.tags.contains(tag), path + ".sizes." + tag, "size requires resource tag");
			resource.sizes[tag] = stock_number(size, path + ".sizes." + tag, 1, 1000000000);
		}
		for (const auto &effect : stock_names(value.value("effects", Json::array()), path + ".effects"))
		{
			stock_require(catalog.effects.contains(effect), path, "unknown inert effect reference " + effect);
			resource.effects.push_back(effect);
		}
		if (value.contains("ammunition"))
		{
			stock_require(resource.tags.contains("ammo"), path, "ammunition profile requires ammo tag");
			(void)compile_ammunition_resource(catalog, id);
		}
		result.emplace(id, std::move(resource));
	}
	validate_resource_catalogue(result);
	return result;
}
RecipeDefinition compile_recipe(const Catalog &catalog, const std::string &id)
{
	const auto path = "recipes." + id;
	stock_require(valid_identifier(id), path, "invalid recipe identity");
	stock_require(catalog.recipes.contains(id), path, "unknown recipe");
	const auto &value = catalog.recipes.at(id);
	stock_fields(value, {"id", "extends", "label", "tags", "inputs", "outputs", "work"}, path);
	RecipeDefinition recipe;
	recipe.id = id;
	recipe.label = value.at("label").get<std::string>();
	stock_require(!recipe.label.empty() && recipe.label.size() <= 128, path, "invalid label");
	recipe.tags = stock_names(value.value("tags", Json::array()), path + ".tags");
	recipe.work = stock_number(value.at("work"), path + ".work", 1, 1000000000);
	for (const auto *key : {"inputs", "outputs"})
	{
		const auto amounts = value.value(key, Json::object());
		stock_require(amounts.is_object() && amounts.size() <= 64, path, "expected bounded resource quantities");
		auto &target = std::string(key) == "inputs" ? recipe.inputs : recipe.outputs;
		for (const auto &[resource, count] : amounts.items())
		{
			stock_require(catalog.resources.contains(resource), path, "unknown exact resource " + resource);
			target[resource] = stock_number(count, path + "." + key + "." + resource, 1);
		}
	}
	stock_require(!recipe.outputs.empty(), path, "recipe requires outputs");
	return recipe;
}
std::map<std::string, RecipeDefinition> compile_recipe_catalog(const Catalog &catalog)
{
	stock_require(catalog.recipes.size() <= 4096, "recipes", "too many recipes");
	std::map<std::string, RecipeDefinition> result;
	for (const auto &[id, value] : catalog.recipes)
		result.emplace(id, compile_recipe(catalog, id));
	return result;
}
StockStoreDefinition compile_stock_store(const Catalog &catalog, const Json &value, const std::string &id)
{
	const auto path = "stores." + id;
	stock_fields(value, {"id", "capacity_tag", "capacity", "filter", "single_resource", "selection", "initial"}, path);
	StockStoreDefinition store;
	store.id = value.value("id", id);
	stock_require(valid_identifier(store.id), path, "invalid store identity");
	store.capacity_tag = value.at("capacity_tag").get<std::string>();
	stock_require(valid_identifier(store.capacity_tag), path, "invalid capacity tag");
	store.capacity = stock_number(value.at("capacity"), path + ".capacity", 1);
	store.single_resource = value.value("single_resource", false);
	store.selection = value.value("selection", std::string{});
	const auto &filter = value.at("filter");
	stock_fields(filter, {"tags", "resources", "require_all", "mode"}, path + ".filter");
	store.filter.tags = stock_names(filter.value("tags", Json::array()), path + ".filter.tags");
	store.filter.resources = stock_names(filter.value("resources", Json::array()), path + ".filter.resources");
	const auto mode = filter.value("mode", std::string("or"));
	stock_require(mode == "or" || mode == "and", path, "invalid filter mode");
	store.filter.require_all = filter.value("require_all", mode == "and");
	stock_require(!filter.contains("mode") || !filter.contains("require_all") ||
	                  store.filter.require_all == (mode == "and"),
	              path + ".filter", "conflicting mode and require_all");
	stock_require(!store.filter.tags.empty() || !store.filter.resources.empty(), path,
	              "store requires acceptance filter");
	const auto initial = value.value("initial", Json::object());
	stock_require(initial.is_object() && initial.size() <= 128, path, "invalid initial stock");
	for (const auto &[resource, count] : initial.items())
		store.initial[resource] = stock_number(count, path + ".initial." + resource);
	const auto resources = compile_resource_catalog(catalog);
	try
	{
		validate_stock_store(store, resources);
	}
	catch (const std::exception &error)
	{
		throw std::runtime_error(path + ": " + error.what());
	}
	stock_require(std::any_of(resources.begin(), resources.end(),
	                          [&](const auto &resource)
	                          {
		                          return stock_filter_accepts(store.filter, resource.second);
	                          }),
	              path, "filter accepts no resource");
	if (store.single_resource && store.filter.tags.empty() && store.filter.resources.size() == 1)
		store.selection = *store.filter.resources.begin();

	return store;
}
ProcessorDefinition compile_processor(const Catalog &catalog, const Json &value, const std::string &id)
{
	const auto path = "processors." + id;
	stock_fields(value,
	             {"id", "equipment", "recipe", "recipes", "recipe_tags", "initial_recipe", "mode", "inputs", "outputs",
	              "work_per_second", "energy_per_second"},
	             path);
	ProcessorDefinition processor;
	processor.id = id;
	processor.equipment = value.value("equipment", id);
	processor.initial_recipe = value.value("initial_recipe", value.value("recipe", std::string{}));
	const auto mode = value.value("mode", std::string("batch"));
	stock_require(mode == "batch" || mode == "continuous", path, "invalid processor mode");
	processor.continuous = mode == "continuous";
	processor.work_per_second =
	    stock_number(value.value("work_per_second", Json(20)), path + ".work_per_second", 0, 1000000000);
	processor.energy_per_second =
	    stock_number(value.value("energy_per_second", Json(0)), path + ".energy_per_second", 0, resource_limit);
	auto ids = stock_names(value.value("recipes", Json::array()), path + ".recipes", 128);
	if (value.contains("recipe"))
		ids.insert(value.at("recipe").get<std::string>());
	const auto tags = stock_names(value.value("recipe_tags", Json::array()), path + ".recipe_tags");
	for (const auto &recipe : ids)
		stock_require(catalog.recipes.contains(recipe), path, "unknown recipe " + recipe);
	for (const auto &[key, raw] : catalog.recipes)
	{
		auto recipe = compile_recipe(catalog, key);
		if (ids.contains(key) || std::any_of(tags.begin(), tags.end(),
		                                     [&](const auto &tag)
		                                     {
			                                     return recipe.tags.contains(tag);
		                                     }))
			processor.recipes.push_back(std::move(recipe));
	}
	stock_require(!processor.recipes.empty() && processor.recipes.size() <= 128 &&
	                  std::any_of(processor.recipes.begin(), processor.recipes.end(),
	                              [&](const auto &r)
	                              {
		                              return r.id == processor.initial_recipe;
	                              }),
	              path, "initial recipe not supported");
	if (processor.continuous)
		for (const auto &recipe : processor.recipes)
		{
			const auto total = recipe.work * 1000;
			auto divisor = total;
			for (const auto *amounts : {&recipe.inputs, &recipe.outputs})
				for (const auto &[resource, amount] : *amounts)
					divisor = std::gcd(divisor, amount);
			const auto period = total / divisor;
			stock_require(period <= processor_ratio_period_limit, path + "." + recipe.id,
			              "continuous recipe period " + std::to_string(period) + " exceeds " +
			                  std::to_string(processor_ratio_period_limit) +
			                  "; choose batch mode or author ratios with a supported exact period");
		}
	for (const auto *key : {"inputs", "outputs"})
	{
		const auto bindings = value.value(key, Json::object());
		stock_require(bindings.is_object() && bindings.size() <= 128, path, "invalid processor bindings");
		auto &target = std::string(key) == "inputs" ? processor.inputs : processor.outputs;
		for (const auto &[resource, store] : bindings.items())
		{
			stock_require(catalog.resources.contains(resource) && store.is_string() &&
			                  !store.get<std::string>().empty() && store.get<std::string>().size() <= 128,
			              path, "invalid exact processor binding");
			target[resource] = store.get<std::string>();
		}
		for (const auto &recipe : processor.recipes)
			for (const auto &[resource, amount] : std::string(key) == "inputs" ? recipe.inputs : recipe.outputs)
				stock_require(target.contains(resource), path, "unbound recipe resource " + resource);
	}
	return processor;
}
std::vector<ProcessorDefinition> compile_processors(const Catalog &catalog, const std::string &entity)
{
	const DirectedCatalog directed_catalog{{entity, compile_directed(catalog, entity)}};
	return compile_processors(catalog, entity, directed_catalog);
}
std::vector<ProcessorDefinition> compile_processors(const Catalog &catalog, const std::string &entity,
                                                    const DirectedCatalog &directed_catalog)
{
	std::vector<ProcessorDefinition> result;
	const auto &hull = catalog.entities.at(entity);
	auto rows = hull.value("processors", Json::array());
	stock_require(rows.is_array() && rows.size() <= 128, entity + ".processors", "expected bounded processor array");
	const auto generic_count = rows.size();
	const auto systems = hull.value("systems", Json::object());
	for (const auto &[name, system] : systems.items())
		if (system.value("kind", std::string{}) == "magazine" && system.contains("processor"))
		{
			auto processor = system.at("processor");
			processor["id"] = name;
			processor["equipment"] = name;
			if (!processor.contains("outputs"))
			{
				processor["outputs"] = Json::object();
				const auto stock = compile_stock_store(catalog, system.at("stock"), name);
				for (const auto &[resource, definition] : compile_resource_catalog(catalog))
					if (stock_filter_accepts(stock.filter, definition))
						processor["outputs"][resource] = "system:" + name;
			}
			rows.push_back(std::move(processor));
		}
	const auto resources = compile_resource_catalog(catalog);
	std::map<std::string, StockStoreDefinition> stores;
	std::map<std::string, std::set<std::string>> aliases;
	auto add_store = [&](const std::string &key, const StockStoreDefinition &stock)
	{
		stores.emplace(key, stock);
		aliases[key].insert(key);
		aliases[stock.id].insert(key);
	};
	if (!rows.empty())
	{
		for (const auto &[name, system] : hull.at("systems").items())
			if (system.value("kind", std::string{}) == "magazine")
				add_store("system:" + name, compile_stock_store(catalog, system.at("stock"), name));
		const auto logistics = compile_logistics(catalog, entity, directed_catalog);
		for (const auto &store : logistics.stores)
		{
			const bool magazine = !store.system.empty() && hull.at("systems").contains(store.system) &&
			                      hull.at("systems").at(store.system).value("kind", std::string{}) == "magazine";
			const auto key = magazine ? "system:" + store.system : "logistics:" + store.id;
			if (!stores.contains(key))
				add_store(key, store.stock);
			aliases[store.id].insert(key);
		}
		if (hull.contains("resources"))
		{
			const auto network = compile_resource_network(hull.at("resources"), resources);
			for (const auto &store : network.stores)
				add_store("energy:" + store.id, store.stock);
			for (const auto &store : network.coolant_sources)
				add_store("coolant:" + store.id, store.stock);
		}
	}
	std::set<std::string> ids;
	std::size_t index = 0;
	for (const auto &row : rows)
	{
		const auto id = row.at("id").get<std::string>();
		stock_require(valid_identifier(id) && ids.insert(id).second, entity, "invalid or duplicate processor");
		auto processor = compile_processor(catalog, row, id);
		stock_require(hull.at("systems").contains(processor.equipment), entity, "processor requires fitted equipment");
		if (processor.energy_per_second > 0)
			stock_require(hull.at("systems").at(processor.equipment).contains("power_store"), entity,
			              "funded processor requires equipment power store");
		for (const auto *bindings : {&processor.inputs, &processor.outputs})
			for (const auto &[resource, address] : *bindings)
			{
				stock_require(aliases.contains(address) && aliases.at(address).size() == 1, entity,
				              "unknown or ambiguous processor store " + address);
				const auto &store = stores.at(*aliases.at(address).begin());
				stock_require(stock_filter_accepts(store.filter, resources.at(resource)), entity,
				              "processor resource rejected by bound store " + address);
			}
		for (const auto &recipe : processor.recipes)
			for (bool output : {false, true})
			{
				std::map<std::string, std::int64_t> sizes;
				std::map<std::string, std::string> types;
				for (const auto &[resource, amount] : output ? recipe.outputs : recipe.inputs)
				{
					const auto key = *aliases.at((output ? processor.outputs : processor.inputs).at(resource)).begin();
					const auto &store = stores.at(key);
					if (store.single_resource)
					{
						stock_require(!types.contains(key) || types.at(key) == resource, entity,
						              "recipe requires multiple types in a single-resource store");
						types[key] = resource;
					}
					if (!processor.continuous)
					{
						const auto size = resources.at(resource).sizes.at(store.capacity_tag);
						stock_require(amount <= (store.capacity - sizes[key]) / size, entity,
						              "recipe batch exceeds bound store capacity");
						sizes[key] += amount * size;
					}
				}
			}

		if (index++ < generic_count)
			result.push_back(std::move(processor));
	}
	return result;
}
} // namespace phoenix
