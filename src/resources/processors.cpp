module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <iterator>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.resources;
namespace phoenix
{
namespace
{
// Fixed 160-bit unsigned arithmetic for capacity inequalities. Five limbs
// cover 128 terms of three bounded 1e12 factors without platform extensions.
struct Wide
{
	std::array<std::uint32_t, 5> limbs{};
	explicit Wide(std::uint64_t value = 0)
	{
		limbs[0] = static_cast<std::uint32_t>(value);
		limbs[1] = static_cast<std::uint32_t>(value >> 32);
	}
	void multiply(std::uint64_t value)
	{
		const std::array<std::uint32_t, 2> factor{static_cast<std::uint32_t>(value),
		                                          static_cast<std::uint32_t>(value >> 32)};
		std::array<std::uint32_t, 5> result{};
		for (std::size_t i = 0; i < 5; ++i)
		{
			std::uint64_t carry = 0;
			std::size_t j = 0;
			for (; j < 2 && i + j < 5; ++j)
			{
				const auto product = static_cast<std::uint64_t>(limbs[i]) * factor[j] + result[i + j] + carry;
				result[i + j] = static_cast<std::uint32_t>(product);
				carry = product >> 32;
			}
			for (auto k = i + j; carry && k < 5; ++k)
			{
				const auto sum = static_cast<std::uint64_t>(result[k]) + carry;
				result[k] = static_cast<std::uint32_t>(sum);
				carry = sum >> 32;
			}
		}
		limbs = result;
	}
	void add(const Wide &other)
	{
		std::uint64_t carry = 0;
		for (std::size_t i = 0; i < 5; ++i)
		{
			const auto sum = static_cast<std::uint64_t>(limbs[i]) + other.limbs[i] + carry;
			limbs[i] = static_cast<std::uint32_t>(sum);
			carry = sum >> 32;
		}
	}
	bool operator<=(const Wide &other) const
	{
		for (int i = 4; i >= 0; --i)
			if (limbs[i] != other.limbs[i])
				return limbs[i] < other.limbs[i];
		return true;
	}
};
Wide product(ResourceAmount a, ResourceAmount b, ResourceAmount c = 1)
{
	Wide result(a);
	result.multiply(b);
	result.multiply(c);
	return result;
}
// All operands are bounded by 1e12. Binary quotient/remainder accumulation
// avoids an overflowing quantity*progress intermediate on native and WASM.
ResourceAmount portion(ResourceAmount quantity, ResourceAmount progress, ResourceAmount total, bool round_up)
{
	ResourceAmount quotient = 0, remainder = 0;
	for (int bit = 39; bit >= 0; --bit)
	{
		quotient *= 2;
		remainder = remainder * 2 + ((quantity >> bit) & 1) * progress;
		quotient += remainder / total;
		remainder %= total;
	}
	return quotient + (round_up && remainder != 0);
}
StockStore &processor_stock(const ProcessorStores &stores, const std::string &name)
{
	auto *store = stores.at(name);
	if (!store)
		throw std::runtime_error("Processor store binding unavailable");
	return *store;
}
ResourceAmount integer(const Json &json, const char *key, ResourceAmount maximum)
{
	const auto &value = json.at(key);
	if (!value.is_number_integer() || value < 0 || value > maximum)
		throw std::runtime_error(std::string("Invalid processor ") + key);
	return value.get<ResourceAmount>();
}
} // namespace
ResourceProcessor::ResourceProcessor(ProcessorDefinition definition) : definition_(std::move(definition))
{
	if (definition_.id.empty() || definition_.id.size() > 128 || definition_.recipes.empty() ||
	    definition_.recipes.size() > 128 || definition_.work_per_second < 0 ||
	    definition_.work_per_second > 1000000000 || definition_.energy_per_second < 0 ||
	    definition_.energy_per_second > resource_limit)
		throw std::runtime_error("Invalid processor definition");
	std::set<std::string> ids;
	for (const auto &recipe : definition_.recipes)
	{
		if (!ids.insert(recipe.id).second || recipe.work < 1 || recipe.work > 1000000000 || recipe.outputs.empty() ||
		    recipe.inputs.size() > 64 || recipe.outputs.size() > 64)
			throw std::runtime_error("Invalid processor recipe");
		for (const auto &[resource, quantity] : recipe.inputs)
			if (quantity < 1 || quantity > resource_limit || !definition_.inputs.contains(resource))
				throw std::runtime_error("Invalid processor ingredient binding");
		for (const auto &[resource, quantity] : recipe.outputs)
			if (quantity < 1 || quantity > resource_limit || !definition_.outputs.contains(resource))
				throw std::runtime_error("Invalid processor product binding");
		if (definition_.continuous)
		{
			auto divisor = recipe.work * 1000;
			for (const auto *quantities : {&recipe.inputs, &recipe.outputs})
				for (const auto &[resource, quantity] : *quantities)
					divisor = std::gcd(divisor, quantity);
			if (recipe.work * 1000 / divisor > processor_ratio_period_limit)
				throw std::runtime_error("Continuous recipe ratio period exceeds supported bound");
		}
	}
	state_.recipe = definition_.initial_recipe;
	(void)recipe();
}
const ProcessorDefinition &ResourceProcessor::definition() const
{
	return definition_;
}
const ProcessorState &ResourceProcessor::state() const
{
	return state_;
}
const RecipeDefinition &ResourceProcessor::recipe() const
{
	const auto found = std::find_if(definition_.recipes.begin(), definition_.recipes.end(),
	                                [&](const auto &r)
	                                {
		                                return r.id == state_.recipe;
	                                });
	if (found == definition_.recipes.end())
		throw std::runtime_error("Unsupported processor recipe");
	return *found;
}
std::string ResourceProcessor::input_claim(const std::string &resource) const
{
	return "processor:" + definition_.id + ":input:" + resource;
}
std::string ResourceProcessor::output_claim(const std::string &resource) const
{
	return "processor:" + definition_.id + ":output:" + resource;
}
ResourceAmount ResourceProcessor::prepare(const ProcessorStores &stores, ResourceAmount maximum_work)
{
	if (maximum_work < 0 || maximum_work > resource_limit || state_.prepared_work)
		throw std::runtime_error("Invalid processor work preparation");
	const auto &r = recipe();
	const auto total = r.work * 1000;
	if (!maximum_work)
		return 0;
	if (!definition_.continuous)
	{
		const auto wanted = (state_.progress + maximum_work + total - 1) / total;
		auto stage = [&](StockTransaction &prepared, ResourceAmount batches)
		{
			const auto additional = batches - state_.committed_batches;
			for (const auto &[resource, quantity] : r.inputs)
				if (additional > resource_limit / quantity ||
				    (additional && !prepared.consume(processor_stock(stores, definition_.inputs.at(resource)), resource,
				                                     quantity * additional)))
					return false;
			for (const auto &[resource, quantity] : r.outputs)
			{
				if (batches > resource_limit / quantity)
					return false;
				auto &store = processor_stock(stores, definition_.outputs.at(resource));
				if (prepared.view(store).capacity_claims().contains(output_claim(resource)))
					prepared.release_capacity(store, output_claim(resource));
				if (!prepared.reserve_capacity(store, output_claim(resource), resource, quantity * batches))
					return false;
			}
			return true;
		};
		ResourceAmount batches = std::max(wanted, state_.committed_batches);
		StockTransaction prepared;
		if (!stage(prepared, batches))
		{
			// Whole batch quantities are linear, so feasible counts form a prefix.
			ResourceAmount low = state_.committed_batches, high = batches;
			while (high - low > 1)
			{
				const auto middle = low + (high - low) / 2;
				StockTransaction candidate;
				if (stage(candidate, middle))
					low = middle;
				else
					high = middle;
			}
			batches = low;
			if (!batches)
				return 0;
			StockTransaction partial;
			if (!stage(partial, batches))
				throw std::runtime_error("Committed processor batches unavailable");
			if (!partial.commit())
				throw std::runtime_error("Processor stock transaction became stale");
		}
		else if (!prepared.commit())
			throw std::runtime_error("Processor stock transaction became stale");
		state_.committed_batches = batches;
		state_.active = true;
		const auto available = batches >= (state_.progress + maximum_work + total - 1) / total
		                           ? maximum_work
		                           : batches * total - state_.progress;
		return state_.prepared_work = available;
	}
	// Limit cumulative ratios before multiplication; native quantities stay
	// within the supported stock bound even for very fast short recipes.
	for (const auto *quantities : {&r.inputs, &r.outputs})
		for (const auto &[resource, quantity] : *quantities)
		{
			const auto limit = product(resource_limit, total);
			if (!(product(quantity, state_.progress + maximum_work) <= limit))
			{
				ResourceAmount low = 0, high = maximum_work;
				while (high - low > 1)
				{
					const auto middle = low + (high - low) / 2;
					if (product(quantity, state_.progress + middle) <= limit)
						low = middle;
					else
						high = middle;
				}
				maximum_work = low;
			}
		}
	if (!maximum_work)
		return 0;
	// Ingredient feasibility is monotonic even when both sides share a store.
	auto ingredients_available = [&](ResourceAmount work)
	{
		for (const auto &[resource, quantity] : r.inputs)
		{
			const auto paid = state_.input_paid.contains(resource) ? state_.input_paid.at(resource) : 0;
			if (portion(quantity, state_.progress + work, total, true) - paid >
			    stores.at(definition_.inputs.at(resource))->available(resource))
				return false;
		}
		return true;
	};
	if (!ingredients_available(maximum_work))
	{
		ResourceAmount low = 0, high = maximum_work;
		while (high - low > 1)
		{
			const auto middle = low + (high - low) / 2;
			if (ingredients_available(middle))
				low = middle;
			else
				high = middle;
		}
		maximum_work = low;
		if (!maximum_work)
			return 0;
	}
	auto stage = [&](StockTransaction &prepared, ResourceAmount work)
	{
		const auto next = state_.progress + work;
		for (const auto &[resource, quantity] : r.inputs)
		{
			const auto paid = state_.input_paid.contains(resource) ? state_.input_paid.at(resource) : 0;
			const auto required = std::max<ResourceAmount>(0, portion(quantity, next, total, true) - paid);
			if (required &&
			    !prepared.consume(processor_stock(stores, definition_.inputs.at(resource)), resource, required))
				return false;
		}
		for (const auto &[resource, quantity] : r.outputs)
		{
			auto &store = processor_stock(stores, definition_.outputs.at(resource));
			if (prepared.view(store).capacity_claims().contains(output_claim(resource)))
				prepared.release_capacity(store, output_claim(resource));
			const auto required =
			    portion(quantity, next, total, true) - portion(quantity, state_.progress, total, false);
			if (required && !prepared.reserve_capacity(store, output_claim(resource), resource, required))
				return false;
		}
		return true;
	};
	StockTransaction whole;
	if (stage(whole, maximum_work))
	{
		if (!whole.commit())
			throw std::runtime_error("Processor stock transaction became stale");
		for (const auto &[resource, quantity] : r.inputs)
			state_.input_paid[resource] =
			    std::max(state_.input_paid[resource], portion(quantity, state_.progress + maximum_work, total, true));
		state_.active = true;
		return state_.prepared_work = maximum_work;
	}
	std::set<StockStore *> input_stores;
	for (const auto &[resource, quantity] : r.inputs)
		input_stores.insert(stores.at(definition_.inputs.at(resource)));
	const bool shared = std::any_of(r.outputs.begin(), r.outputs.end(),
	                                [&](const auto &output)
	                                {
		                                return input_stores.contains(stores.at(definition_.outputs.at(output.first)));
	                                });
	if (!shared)
	{
		// Without input/output aliasing, consuming ingredients cannot create
		// output space. Ordinary capacity feasibility is a monotonic prefix.
		ResourceAmount low = 0, high = maximum_work;
		while (high - low > 1)
		{
			const auto middle = low + (high - low) / 2;
			StockTransaction candidate;
			if (stage(candidate, middle))
				low = middle;
			else
				high = middle;
		}
		if (!low)
			return 0;
		StockTransaction partial;
		if (!stage(partial, low))
			return 0;
		if (!partial.commit())
			throw std::runtime_error("Processor stock transaction became stale");
		for (const auto &[resource, quantity] : r.inputs)
			state_.input_paid[resource] =
			    std::max(state_.input_paid[resource], portion(quantity, state_.progress + low, total, true));
		state_.active = true;
		return state_.prepared_work = low;
	}
	// Every ratio repeats after this bounded number of milli-work units.
	// Within a residue class quantities are linear in the number of periods;
	// prepaid-input activation points partition that line into linear pieces.
	auto divisor = total;
	for (const auto *quantities : {&r.inputs, &r.outputs})
		for (const auto &[resource, quantity] : *quantities)
			divisor = std::gcd(divisor, quantity);
	const auto period = total / divisor;
	std::set<StockStore *> unique;
	for (const auto &[name, store] : stores)
		unique.insert(store);
	for (const auto *quantities : {&r.inputs, &r.outputs})
		for (const auto &[resource, quantity] : *quantities)
			if (!stores.at((quantities == &r.inputs ? definition_.inputs : definition_.outputs).at(resource))
			         ->accepts(resource))
				return 0;
	auto capacity_fits = [&](const StockStore *store, ResourceAmount work)
	{
		auto used = store->occupied() + store->reserved_capacity();
		Wide needed, freed(store->definition().capacity);
		for (const auto &[resource, quantity] : r.inputs)
			if (stores.at(definition_.inputs.at(resource)) == store)
			{
				const auto paid = state_.input_paid.contains(resource) ? state_.input_paid.at(resource) : 0;
				freed.add(
				    product(std::max<ResourceAmount>(0, portion(quantity, state_.progress + work, total, true) - paid),
				            store->size(resource)));
			}
		for (const auto &[resource, quantity] : r.outputs)
			if (stores.at(definition_.outputs.at(resource)) == store)
			{
				const auto size = store->size(resource);
				if (const auto claim = store->capacity_claims().find(output_claim(resource));
				    claim != store->capacity_claims().end())
					used -= claim->second.quantity * size;
				needed.add(product(portion(quantity, state_.progress + work, total, true) -
				                       portion(quantity, state_.progress, total, false),
				                   size));
			}
		needed.add(Wide(used));
		return needed <= freed;
	};
	ResourceAmount low = 0;
	for (ResourceAmount residue = 1; residue <= std::min(period, maximum_work); ++residue)
	{
		const auto last = (maximum_work - residue) / period;
		std::set<ResourceAmount> breaks{0, last + 1};
		for (const auto &[resource, quantity] : r.inputs)
		{
			const auto paid = state_.input_paid.contains(resource) ? state_.input_paid.at(resource) : 0;
			const auto first = portion(quantity, state_.progress + residue, total, true), step = quantity / divisor;
			if (paid >= first)
			{
				const auto activation = (paid - first) / step + 1;
				if (activation <= last)
					breaks.insert(activation);
			}
		}
		for (auto begin = breaks.begin(), end = std::next(begin); end != breaks.end(); ++begin, ++end)
		{
			ResourceAmount lower = *begin, upper = *end - 1;
			if (residue + upper * period <= low)
				continue;
			bool feasible = true;
			for (const auto *store : unique)
			{
				const auto fits = [&](ResourceAmount cycles)
				{
					return capacity_fits(store, residue + cycles * period);
				};
				const bool left = fits(lower), right = fits(upper);
				if (!left && !right)
				{
					feasible = false;
					break;
				}
				if (left == right)
					continue;
				auto a = lower, b = upper;
				while (b - a > 1)
				{
					const auto middle = a + (b - a) / 2;
					if (fits(middle) == left)
						a = middle;
					else
						b = middle;
				}
				if (left)
					upper = a;
				else
					lower = b;
			}
			if (feasible)
				low = std::max(low, residue + upper * period);
		}
	}
	if (!low)
		return 0;
	StockTransaction partial;
	if (!stage(partial, low))
		return 0;
	if (!partial.commit())
		throw std::runtime_error("Processor stock transaction became stale");
	for (const auto &[resource, quantity] : r.inputs)
		state_.input_paid[resource] =
		    std::max(state_.input_paid[resource], portion(quantity, state_.progress + low, total, true));
	state_.active = true;
	return state_.prepared_work = low;
}
void ResourceProcessor::advance(const ProcessorStores &stores, ResourceAmount funded_work)
{
	if (funded_work < 0 || funded_work > state_.prepared_work)
		throw std::runtime_error("Processor work was not prepared");
	if (!state_.active)
		return;
	const auto &r = recipe();
	const auto total = r.work * 1000, next = state_.progress + funded_work;
	StockTransaction candidate;
	if (definition_.continuous)
	{
		for (const auto &[resource, quantity] : r.outputs)
		{
			auto &store = processor_stock(stores, definition_.outputs.at(resource));
			if (candidate.view(store).capacity_claims().contains(output_claim(resource)))
				candidate.release_capacity(store, output_claim(resource));
			const auto produced =
			    portion(quantity, next, total, false) - portion(quantity, state_.progress, total, false);
			const auto reserved = portion(quantity, next, total, true) - portion(quantity, next, total, false);
			if ((produced && !candidate.add(store, resource, produced)) ||
			    (reserved && !candidate.reserve_capacity(store, output_claim(resource), resource, reserved)))
				throw std::runtime_error("Reserved processor output unavailable");
		}
	}
	else if (next >= total)
		for (const auto &[resource, quantity] : r.outputs)
		{
			auto &store = processor_stock(stores, definition_.outputs.at(resource));
			const auto completed = next / total, remaining = state_.committed_batches - completed;
			if (!candidate.release_capacity(store, output_claim(resource)) ||
			    !candidate.add(store, resource, quantity * completed) ||
			    (remaining &&
			     !candidate.reserve_capacity(store, output_claim(resource), resource, quantity * remaining)))
				throw std::runtime_error("Reserved batch output unavailable");
		}
	if (!candidate.commit())
		throw std::runtime_error("Processor stock transaction became stale");
	const auto completed = next / total;
	state_.progress = next % total;
	if (definition_.continuous)
	{
		for (const auto &[resource, quantity] : r.inputs)
		{
			auto &paid = state_.input_paid[resource];
			paid -= quantity * completed;
			if (!paid)
				state_.input_paid.erase(resource);
		}
		state_.active = state_.progress > 0 || !state_.input_paid.empty();
	}
	else
	{
		state_.committed_batches -= completed;
		state_.active = state_.committed_batches > 0;
	}
	state_.prepared_work = 0;
}
bool ResourceProcessor::select(const ProcessorStores &stores, const std::string &id)
{
	if (id == state_.recipe)
		return true;
	if (!can_select(stores, id) || state_.selection_revision == 9000000000000000ULL)
		return false;
	StockTransaction candidate;
	const auto &r = recipe();
	for (const auto &[resource, quantity] : r.inputs)
	{
		auto &store = processor_stock(stores, definition_.inputs.at(resource));
		if (candidate.view(store).stock_claims().contains(input_claim(resource)))
			candidate.release_stock(store, input_claim(resource));
	}
	for (const auto &[resource, quantity] : r.outputs)
	{
		auto &store = processor_stock(stores, definition_.outputs.at(resource));
		if (candidate.view(store).capacity_claims().contains(output_claim(resource)))
			candidate.release_capacity(store, output_claim(resource));
	}
	if (!candidate.commit())
		throw std::runtime_error("Processor stock transaction became stale");
	const auto revision = state_.selection_revision + 1;
	state_ = {};
	state_.recipe = id;
	state_.selection_revision = revision;
	return true;
}
bool ResourceProcessor::can_select(const ProcessorStores &stores, const std::string &id) const
{
	const auto selected = std::find_if(definition_.recipes.begin(), definition_.recipes.end(),
	                                   [&](const auto &r)
	                                   {
		                                   return r.id == id;
	                                   });
	if (selected == definition_.recipes.end())
		return false;
	for (const auto &[resource, quantity] : selected->inputs)
		if (!stores.at(definition_.inputs.at(resource))->accepts(resource))
			return false;
	for (const auto &[resource, quantity] : selected->outputs)
		if (!stores.at(definition_.outputs.at(resource))->accepts(resource))
			return false;
	return true;
}
ResourceAmount ResourceProcessor::work_budget(ResourceAmount effective_rate)
{
	if (effective_rate < 0 || effective_rate > 1000000000)
		throw std::runtime_error("Invalid processor rate");
	return milli_work_budget(effective_rate * 1000);
}
ResourceAmount ResourceProcessor::milli_work_budget(ResourceAmount effective_milli_rate)
{
	if (effective_milli_rate < 0 || effective_milli_rate > resource_limit)
		throw std::runtime_error("Invalid processor milli-rate");
	if (!effective_milli_rate)
		return 0;
	const auto scaled = effective_milli_rate + state_.rate_remainder;
	state_.rate_remainder = scaled % resource_tick_hz;
	return scaled / resource_tick_hz;
}
ResourceAmount ResourceProcessor::funded_work(ResourceAmount supply_permille)
{
	if (supply_permille < 0 || supply_permille > 1000)
		throw std::runtime_error("Invalid processor funding");
	const auto scaled = state_.prepared_work * supply_permille + state_.funding_remainder;
	state_.funding_remainder = scaled % 1000;
	return scaled / 1000;
}
ResourceAmount ResourceProcessor::operating_rate(ResourceAmount maximum_work) const
{
	if (!state_.prepared_work)
		return 0;
	if (maximum_work < state_.prepared_work || maximum_work > resource_limit)
		throw std::runtime_error("Invalid processor operating quote");
	return portion(definition_.energy_per_second, state_.prepared_work, maximum_work, true);
}
Json ResourceProcessor::save() const
{
	return {{"recipe", state_.recipe},
	        {"progress", state_.progress},
	        {"prepared_work", state_.prepared_work},
	        {"rate_remainder", state_.rate_remainder},
	        {"funding_remainder", state_.funding_remainder},
	        {"input_paid", state_.input_paid},
	        {"active", state_.active},
	        {"selection_revision", state_.selection_revision},
	        {"committed_batches", state_.committed_batches}};
}
void ResourceProcessor::restore(const Json &saved, const ProcessorStores &stores)
{
	ResourceProcessor candidate(definition_);
	auto &state = candidate.state_;
	if (!saved.is_object() || saved.size() != 9)
		throw std::runtime_error("Invalid processor continuation fields");
	state.selection_revision = integer(saved, "selection_revision", 9000000000000000LL);
	if (!state.selection_revision)
		throw std::runtime_error("Invalid processor selection revision");
	state.recipe = saved.at("recipe").get<std::string>();
	const auto &r = candidate.recipe();
	const auto total = r.work * 1000;
	state.progress = integer(saved, "progress", total - 1);
	state.prepared_work = integer(saved, "prepared_work", resource_limit);
	state.committed_batches = integer(saved, "committed_batches", resource_limit / 1000 + 1);
	for (const auto *quantities : {&r.inputs, &r.outputs})
		for (const auto &[resource, quantity] : *quantities)
			if (!(product(quantity, state.progress + state.prepared_work) <= product(resource_limit, total)))
				throw std::runtime_error("Processor prepared quantities exceed stock bounds");
	if (definition_.continuous ? state.committed_batches != 0
	                           : state.committed_batches < (state.progress + state.prepared_work + total - 1) / total)
		throw std::runtime_error("Invalid processor batch commitments");
	state.rate_remainder = integer(saved, "rate_remainder", resource_tick_hz - 1);
	state.funding_remainder = integer(saved, "funding_remainder", 999);
	const auto &paid = saved.at("input_paid");
	if (!paid.is_object() || paid.size() > r.inputs.size() || (!definition_.continuous && !paid.empty()))
		throw std::runtime_error("Invalid prepaid processor ingredients");
	for (const auto &[resource, value] : paid.items())
	{
		if (!r.inputs.contains(resource) || !value.is_number_integer() || value <= 0 || value > resource_limit)
			throw std::runtime_error("Invalid prepaid processor ingredient quantity");
		state.input_paid[resource] = value.get<ResourceAmount>();
	}
	if (definition_.continuous)
		for (const auto &[resource, quantity] : r.inputs)
			if ((state.input_paid.contains(resource) ? state.input_paid.at(resource) : 0) <
			    portion(quantity, state.progress + state.prepared_work, total, true))
				throw std::runtime_error("Processor ingredient payment missing");
	state.active = saved.at("active").get<bool>();
	if (!definition_.continuous && state.active != (state.committed_batches > 0))
		throw std::runtime_error("Invalid active batch commitments");
	if ((!state.active && (state.progress || state.prepared_work || !state.input_paid.empty())) ||
	    (definition_.continuous && state.active && !state.progress && !state.prepared_work && state.input_paid.empty()))
		throw std::runtime_error("Inconsistent processor progress");
	auto validate = [&](const auto &claims, const std::string &key, const std::string &resource, ResourceAmount amount)
	{
		const auto found = claims.find(key);
		if (amount ? found == claims.end() || found->second != StockClaim{resource, amount} : found != claims.end())
			throw std::runtime_error("Processor reservation disagrees with continuation");
	};
	for (const auto &[resource, quantity] : r.inputs)
	{
		validate(stores.at(definition_.inputs.at(resource))->stock_claims(), input_claim(resource), resource, 0);
	}
	for (const auto &[resource, quantity] : r.outputs)
	{
		if (!definition_.continuous && state.committed_batches > resource_limit / quantity)
			throw std::runtime_error("Processor batch output exceeds stock bound");
		const auto amount = !state.active ? 0
		                    : definition_.continuous
		                        ? portion(quantity, state.progress + state.prepared_work, total, true) -
		                              portion(quantity, state.progress, total, false)
		                        : quantity * state.committed_batches;
		validate(stores.at(definition_.outputs.at(resource))->capacity_claims(), output_claim(resource), resource,
		         amount);
	}
	state_ = state;
}
} // namespace phoenix
