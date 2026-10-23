module;
#include <optional>
#include <variant>
#include <map>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <queue>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.resources;

namespace phoenix
{
namespace
{
#include "validation.hpp"
constexpr ResourceAmount million = 1000000;
ResourceAmount funded_permille(ResourceAmount funded, ResourceAmount requested)
{
	if (!requested)
		return 0;
	ResourceAmount result = 0, remainder = 0;
	for (int bit = 9; bit >= 0; --bit)
	{
		result *= 2;
		remainder *= 2;
		if ((1000 >> bit) & 1)
			remainder += funded;
		result += remainder / requested;
		remainder %= requested;
	}
	return result;
}
struct ResourceFlowGraph
{
	struct Edge
	{
		std::size_t to, reverse;
		ResourceAmount residual, initial;
	};
	struct Handle
	{
		std::size_t node, edge;
	};
	std::vector<std::vector<Edge>> edges;
	explicit ResourceFlowGraph(std::size_t nodes) : edges(nodes)
	{
	}
	Handle add(std::size_t from, std::size_t to, ResourceAmount capacity)
	{
		Handle h{from, edges[from].size()};
		const auto reverse = edges[to].size();
		edges[from].push_back({to, reverse, capacity, capacity});
		edges[to].push_back({from, h.edge, 0, 0});
		return h;
	}
	ResourceAmount used(Handle h) const
	{
		const auto &e = edges[h.node][h.edge];
		return e.initial - e.residual;
	}
	ResourceAmount resolve(std::size_t source, std::size_t sink, ResourceAmount limit)
	{
		ResourceAmount total = 0;
		while (total < limit)
		{
			std::vector<Handle> previous(edges.size(), {edges.size(), 0});
			std::queue<std::size_t> pending;
			pending.push(source);
			previous[source] = {source, 0};
			while (!pending.empty() && previous[sink].node == edges.size())
			{
				auto at = pending.front();
				pending.pop();
				for (std::size_t i = 0; i < edges[at].size(); ++i)
				{
					const auto &e = edges[at][i];
					if (e.residual > 0 && previous[e.to].node == edges.size())
					{
						previous[e.to] = {at, i};
						pending.push(e.to);
					}
				}
			}
			if (previous[sink].node == edges.size())
				break;
			auto flow = limit - total;
			for (auto at = sink; at != source; at = previous[at].node)
				flow = std::min(flow, edges[previous[at].node][previous[at].edge].residual);
			for (auto at = sink; at != source; at = previous[at].node)
			{
				auto &e = edges[previous[at].node][previous[at].edge];
				e.residual -= flow;
				edges[e.to][e.reverse].residual += flow;
			}
			total += flow;
		}
		return total;
	}
};
void remove_resource_circulations(const ResourceNetworkDefinition &definition, std::vector<ResourceAmount> &flows)
{
	// A directed conduit cycle transports no net energy. Remove it before deriving
	// paid source/destination flow; it must not manufacture charge heat or capacity use.
	struct Frame
	{
		ResourceIndex store;
		std::size_t next_link = 0;
	};
	for (;;)
	{
		std::vector<unsigned char> colour(definition.stores.size());
		std::vector<ResourceIndex> previous(definition.stores.size(), no_resource);
		bool removed = false;
		for (ResourceIndex start = 0; start < definition.stores.size() && !removed; ++start)
		{
			if (colour[start])
				continue;
			std::vector<Frame> stack{{start, 0}};
			colour[start] = 1;
			while (!stack.empty() && !removed)
			{
				auto &frame = stack.back();
				if (frame.next_link == definition.links.size())
				{
					colour[frame.store] = 2;
					stack.pop_back();
					continue;
				}
				const auto edge = static_cast<ResourceIndex>(frame.next_link++);
				const auto &link = definition.links[edge];
				if (!flows[edge] || link.from != frame.store)
					continue;
				if (!colour[link.to])
				{
					previous[link.to] = edge;
					colour[link.to] = 1;
					stack.push_back({link.to, 0});
					continue;
				}
				if (colour[link.to] != 1)
					continue;
				auto quantity = flows[edge];
				for (auto at = frame.store; at != link.to; at = definition.links[previous[at]].from)
					quantity = std::min(quantity, flows[previous[at]]);
				flows[edge] -= quantity;
				for (auto at = frame.store; at != link.to; at = definition.links[previous[at]].from)
					flows[previous[at]] -= quantity;
				removed = true;
			}
		}
		if (!removed)
			return;
	}
}
ResourceAmount divided(ResourceAmount value, ResourceAmount divisor, ResourceAmount &remainder)
{
	const auto total = value + remainder;
	remainder = total % divisor;
	return total / divisor;
}
ResourceAmount per_tick(ResourceAmount rate, ResourceAmount &remainder)
{
	return divided(rate, resource_tick_hz, remainder);
}
ResourceAmount scaled(ResourceAmount value, ResourceAmount ppm, ResourceAmount &remainder)
{
	return divided(value * ppm, million, remainder);
}
void bounded(ResourceAmount value, const char *what, ResourceAmount maximum = resource_limit)
{
	if (value < 0 || value > maximum)
		throw std::runtime_error(std::string("resources: invalid ") + what);
}
} // namespace

ResourceNetwork::ResourceNetwork(ResourceNetworkDefinition definition, std::uint64_t seed)
    : definition_(std::move(definition)), random_(seed ? seed : 1)
{
	for (const auto &d : definition_.stores)
	{
		ResourceStoreState s;
		s.energy = d.initial;
		s.charge_limit = d.charge_rate;
		s.discharge_limit = d.discharge_rate;
		s.heat = d.initial_heat;
		s.enabled = d.enabled;
		s.thermal_stop = d.stop_heat && s.heat >= d.stop_heat;
		stores_.push_back(s);
	}
	for (const auto &d : definition_.generators)
	{
		ResourceGeneratorState s;
		s.commanded_rate = d.initial_output;
		s.actual_rate = d.initial_output;
		s.enabled = d.enabled;
		generators_.push_back(s);
	}
	for (const auto &d : definition_.groups)
	{
		ResourceGroupState s;
		s.budget_rate = d.budget_rate;
		groups_.push_back(s);
	}
	for (const auto &d : definition_.links)
	{
		ResourceLinkState s;
		s.isolated = d.isolated;
		links_.push_back(s);
	}
	for (const auto &d : definition_.coolant_sources)
	{
		CoolantSourceState s;
		s.remaining = d.initial;
		coolant_sources_.push_back(s);
	}
	for (const auto &d : definition_.cooling_links)
	{
		CoolingLinkState s;
		s.allocation_rate = d.allocation_rate;
		s.isolated = d.isolated;
		cooling_links_.push_back(s);
	}
	telemetry_.resize(stores_.size());
	charge_left_.resize(stores_.size());
	discharge_left_.resize(stores_.size());
	demand_.resize(stores_.size());
	initial_energy_.resize(stores_.size());
	initial_heat_.resize(stores_.size());
	link_left_.resize(links_.size());
	group_left_.resize(groups_.size());
}
const ResourceNetworkDefinition &ResourceNetwork::definition() const
{
	return definition_;
}
const std::vector<ResourceStoreState> &ResourceNetwork::stores() const
{
	return stores_;
}
const std::vector<ResourceGeneratorState> &ResourceNetwork::generators() const
{
	return generators_;
}
const std::vector<ResourceGroupState> &ResourceNetwork::groups() const
{
	return groups_;
}
const std::vector<ResourceLinkState> &ResourceNetwork::links() const
{
	return links_;
}
const std::vector<CoolantSourceState> &ResourceNetwork::coolant_sources() const
{
	return coolant_sources_;
}
const std::vector<CoolingLinkState> &ResourceNetwork::cooling_links() const
{
	return cooling_links_;
}
const std::vector<ResourceStoreTelemetry> &ResourceNetwork::telemetry() const
{
	return telemetry_;
}
std::uint64_t ResourceNetwork::ticks() const
{
	return ticks_;
}
void ResourceNetwork::request_operation(ResourceIndex i, ResourceAmount rate, ResourceAmount discrete)
{
	bounded(rate, "operation rate");
	bounded(discrete, "discrete energy");
	auto &s = stores_.at(i);
	s.requested_rate = rate;
	s.discrete_request = discrete;
}
void ResourceNetwork::set_store_enabled(ResourceIndex i, bool enabled)
{
	stores_.at(i).enabled = enabled;
}
void ResourceNetwork::set_storage_limits(ResourceIndex i, ResourceAmount charge, ResourceAmount discharge)
{
	const auto &d = definition_.stores.at(i);
	bounded(charge, "storage charge limit", d.charge_rate);
	bounded(discharge, "storage discharge limit", d.discharge_rate);
	stores_.at(i).charge_limit = charge;
	stores_[i].discharge_limit = discharge;
}
void ResourceNetwork::set_overdrive(ResourceIndex i, bool enabled)
{
	stores_.at(i).overdrive = enabled;
}
void ResourceNetwork::set_thermal_override(ResourceIndex i, bool enabled)
{
	if (enabled && !definition_.stores.at(i).allows_override)
		throw std::runtime_error("resources: unsupported thermal override");
	stores_.at(i).thermal_override = enabled;
}
void ResourceNetwork::set_generator(ResourceIndex i, ResourceAmount rate, bool enabled)
{
	bounded(rate, "generator output", definition_.generators.at(i).maximum_rate);
	auto &s = generators_.at(i);
	s.commanded_rate = rate;
	s.enabled = enabled;
}
void ResourceNetwork::set_group_budget(ResourceIndex i, ResourceAmount rate)
{
	bounded(rate, "group budget");
	groups_.at(i).budget_rate = rate;
}
void ResourceNetwork::set_link_isolated(ResourceIndex i, bool isolated)
{
	links_.at(i).isolated = isolated;
}
void ResourceNetwork::set_cooling(ResourceIndex i, ResourceAmount rate, bool isolated)
{
	const auto &d = definition_.cooling_links.at(i);
	bounded(rate, "cooling allocation", d.capacity_rate);
	ResourceAmount total = rate;
	for (ResourceIndex k = 0; k < cooling_links_.size(); ++k)
		if (k != i && definition_.cooling_links[k].source == d.source)
			total += cooling_links_[k].allocation_rate;
	if (total > definition_.coolant_sources[d.source].removal_rate)
		throw std::runtime_error("resources: cooling reservations exceed provider capacity");
	cooling_links_[i].allocation_rate = rate;
	cooling_links_[i].isolated = isolated;
}
std::uint64_t ResourceNetwork::random_next()
{
	random_ ^= random_ << 13;
	random_ ^= random_ >> 7;
	random_ ^= random_ << 17;
	return random_;
}
bool ResourceNetwork::operational(ResourceIndex i) const
{
	const auto &s = stores_[i];
	const auto &d = definition_.stores[i];
	return s.enabled && (!s.thermal_stop || s.thermal_override) && (!d.stress_limit || s.stress < d.stress_limit);
}
void ResourceNetwork::add_heat(ResourceIndex i, ResourceAmount heat)
{
	heat = std::min(heat, resource_limit - stores_[i].heat);
	stores_[i].heat += heat;
	telemetry_[i].heat_added += heat;
}
void ResourceNetwork::dormant_tick()
{
	std::vector<bool> enabled;
	for (auto &g : generators_)
	{
		enabled.push_back(g.enabled);
		g.enabled = false;
		g.actual_rate = 0;
	}
	for (auto &s : stores_)
		s.requested_rate = s.discrete_request = 0;
	tick();
	for (std::size_t i = 0; i < generators_.size(); ++i)
		generators_[i].enabled = enabled[i];
}
ResourceAmount ResourceNetwork::transfer_external(ResourceNetwork &destination, ResourceIndex source,
                                                  ResourceIndex target, ResourceAmount offered)
{
	if (&destination == this || offered < 0 || offered > resource_limit || source >= stores_.size() ||
	    target >= destination.stores_.size())
		throw std::runtime_error("Invalid external resource transfer");
	if (!operational(source))
		return 0;
	const auto available = std::min({offered, discharge_left_[source], stores_[source].energy});
	const auto received = destination.receive_external(target, available);
	stores_[source].energy -= received;
	discharge_left_[source] -= received;
	telemetry_[source].outgoing += received;
	return received;
}
ResourceAmount ResourceNetwork::receive_external(ResourceIndex i, ResourceAmount offered)
{
	if (i >= stores_.size() || offered < 0)
		throw std::runtime_error("Invalid external charge");
	if (!operational(i))
		return 0;
	auto &s = stores_[i];
	const auto &d = definition_.stores[i];
	const auto received = std::min({offered, charge_left_[i], d.capacity - s.energy});
	s.energy += received;
	charge_left_[i] -= received;
	telemetry_[i].accepted_incoming += received;
	add_heat(i, scaled(received, d.charge_heat_ppm, s.charge_heat_remainder));
	return received;
}
void ResourceNetwork::debit_operation(ResourceIndex i, ResourceAmount amount, bool discrete)
{
	stores_[i].energy -= amount;
	discharge_left_[i] -= amount;
	telemetry_[i].consumed += amount;
	telemetry_[i].discrete_paid |= discrete;
}
void ResourceNetwork::add_consumption_heat(ResourceIndex i, ResourceAmount consumed, ResourceAmount depleted)
{
	auto &s = stores_[i];
	const auto &d = definition_.stores[i];
	auto heat = scaled(consumed, d.operation_heat_ppm, s.operation_heat_remainder);
	if (s.overdrive)
		heat = divided(heat * d.overdrive_heat_permille, 1000, s.overdrive_heat_remainder);
	add_heat(i, heat);
	add_heat(i, scaled(depleted, d.depletion_heat_ppm, s.depletion_heat_remainder));
}
bool ResourceNetwork::spend_external(ResourceIndex i, ResourceAmount amount)
{
	bounded(amount, "external operating payment");
	if (i >= stores_.size())
		throw std::runtime_error("Invalid external payer");
	if (!operational(i) || amount > stores_[i].energy || amount > discharge_left_[i])
		return false;
	auto &s = stores_[i];
	const auto &d = definition_.stores[i];
	debit_operation(i, amount, true);
	add_consumption_heat(i, amount, amount);
	if (d.stop_heat && s.heat >= d.stop_heat)
		s.thermal_stop = true;
	return true;
}
void ResourceNetwork::apply_operating_heat(ResourceIndex store, ResourceAmount heat)
{
	bounded(heat, "paid operating heat");
	const auto &definition = definition_.stores.at(store);
	add_heat(store, heat);
	if (definition.stop_heat && stores_[store].heat >= definition.stop_heat)
		stores_[store].thermal_stop = true;
}

ResourceAmount ResourceNetwork::dissipate(ResourceIndex i, ResourceAmount amount, bool heat)
{
	if (amount < 0 || amount > resource_limit)
		throw std::runtime_error("Invalid reserve dissipation");
	auto &s = stores_.at(i);
	const auto paid = std::min(s.energy, amount);
	s.energy -= paid;
	telemetry_.at(i).lost += paid;
	if (heat)
		add_heat(i, paid);
	return paid;
}

void ResourceNetwork::tick()
{
	++ticks_;
	for (ResourceIndex i = 0; i < stores_.size(); ++i)
	{
		auto &s = stores_[i];
		const auto &d = definition_.stores[i];
		telemetry_[i] = {};
		initial_energy_[i] = s.energy;
		initial_heat_[i] = s.heat;
		if (d.stop_heat && s.heat >= d.stop_heat)
			s.thermal_stop = true;
		else if (s.thermal_stop && s.heat <= d.recover_heat)
			s.thermal_stop = false;
		charge_left_[i] = per_tick(s.charge_limit, s.charge_remainder);
		discharge_left_[i] = per_tick(s.discharge_limit, s.discharge_remainder);
		const auto fullness = s.energy * million / d.capacity;
		const auto loss_ppm = d.loss_empty_ppm + (d.loss_full_ppm - d.loss_empty_ppm) * fullness / million;
		const auto loss =
		    std::min(s.energy, divided(s.energy * loss_ppm, million * resource_tick_hz, s.loss_remainder));
		s.energy -= loss;
		telemetry_[i].lost = loss;
		const auto rate = operational(i) ? s.requested_rate : 0;
		const auto work = rate * (s.overdrive ? d.overdrive_permille : 1000);
		telemetry_[i].work_requested = work;
		const auto unfunded = work ? std::max<ResourceAmount>(0, work - s.work_credit) : 0;
		const auto continuous = (unfunded + resource_work_unit - 1) / resource_work_unit;
		demand_[i] = std::min(resource_limit, continuous + (operational(i) ? s.discrete_request : 0));
		telemetry_[i].requested = demand_[i];
	}
	// Pay affordable continuous work first. This opens only genuinely paid headroom,
	// including for a full generator capacitor; discrete work remains all-or-nothing.
	for (ResourceIndex i = 0; i < stores_.size(); ++i)
	{
		auto &s = stores_[i];
		const auto continuous = std::max<ResourceAmount>(0, demand_[i] - (operational(i) ? s.discrete_request : 0));
		const auto paid = std::min({continuous, s.energy, discharge_left_[i]});
		debit_operation(i, paid, false);
		demand_[i] -= paid;
	}
	for (ResourceIndex i = 0; i < links_.size(); ++i)
		link_left_[i] = links_[i].isolated ? 0 : per_tick(definition_.links[i].capacity_rate, links_[i].tick_remainder);
	for (ResourceIndex i = 0; i < generators_.size(); ++i)
	{
		auto &s = generators_[i];
		const auto &d = definition_.generators[i];
		const auto &equipment = definition_.stores[d.store];
		auto goal = s.enabled && operational(d.store) ? s.commanded_rate : 0;
		if (stores_[d.store].overdrive)
			goal = std::min(resource_limit, goal * equipment.overdrive_permille / 1000);
		const auto change = per_tick(d.response_rate, s.response_remainder);
		if (s.actual_rate < goal)
			s.actual_rate += std::min(change, goal - s.actual_rate);
		else
			s.actual_rate -= std::min(change, s.actual_rate - goal);
		auto generated = per_tick(s.actual_rate, s.generation_remainder);
		// Protection suppresses actual generation immediately, even while the internal response decays.
		if (!s.enabled || !operational(d.store))
			generated = 0;
		auto &store = stores_[d.store];
		const auto &store_d = definition_.stores[d.store];
		generated = std::min({generated, charge_left_[d.store], store_d.capacity - store.energy});
		store.energy += generated;
		charge_left_[d.store] -= generated;
		telemetry_[d.store].generated += generated;
		auto generation_heat = scaled(generated, d.heat_ppm, s.heat_remainder);
		if (store.overdrive)
			generation_heat =
			    divided(generation_heat * equipment.overdrive_heat_permille, 1000, store.overdrive_heat_remainder);
		add_heat(d.store, generation_heat);
	}
	// Coarse roots reserve source flow. Fine siblings reserve subdivisions, with the same cut contract.
	auto sustainable_export_rate = [&](ResourceIndex i)
	{
		const auto &s = stores_[i];
		const auto &d = definition_.stores[i];
		if (!operational(i))
			return ResourceAmount{0};
		auto own = s.requested_rate;
		if (s.overdrive)
			own = (own * d.overdrive_permille + 999) / 1000;
		own = std::min(resource_limit, own + s.discrete_request * resource_tick_hz);
		ResourceAmount generation = 0;
		for (ResourceIndex g = 0; g < generators_.size(); ++g)
			if (definition_.generators[g].store == i && generators_[g].enabled)
				generation += generators_[g].actual_rate;
		generation = std::min(generation, s.charge_limit);
		const auto reserves = std::max<ResourceAmount>(0, s.energy - demand_[i]) * resource_tick_hz;
		return std::min(std::max<ResourceAmount>(0, s.discharge_limit - own), reserves + generation);
	};
	ResourceAmount supply = 0;
	for (ResourceIndex i = 0; i < stores_.size(); ++i)
	{
		bool source = false;
		for (const auto &link : definition_.links)
			if (link.from == i)
			{
				source = true;
				break;
			}
		if (source && operational(i))
			supply += sustainable_export_rate(i);
	}
	auto feasible = [&](const std::vector<ResourceIndex> &active, ResourceAmount requested)
	{
		const auto group_base = stores_.size() * 2, source = group_base + groups_.size(), sink = source + 1;
		ResourceFlowGraph graph(sink + 1);
		for (ResourceIndex i = 0; i < stores_.size(); ++i)
		{
			if (!operational(i))
				continue;
			bool receiving = false;
			for (auto group = definition_.stores[i].group; group != no_resource;
			     group = definition_.groups[group].parent)
				if (std::find(active.begin(), active.end(), group) != active.end())
				{
					receiving = true;
					break;
				}
			if (!receiving)
				graph.add(source, i * 2 + 1, sustainable_export_rate(i));
			graph.add(i * 2, i * 2 + 1, resource_limit * 4096);
			for (auto group = definition_.stores[i].group; group != no_resource;
			     group = definition_.groups[group].parent)
				if (std::find(active.begin(), active.end(), group) != active.end())
				{
					graph.add(i * 2, group_base + group, resource_limit * 4096);
					break;
				}
		}
		for (ResourceIndex i = 0; i < links_.size(); ++i)
			if (!links_[i].isolated)
			{
				const auto &d = definition_.links[i];
				if (operational(d.from) && operational(d.to))
					graph.add(d.from * 2 + 1, d.to * 2, d.capacity_rate);
			}
		for (auto group : active)
			graph.add(group_base + group, sink, groups_[group].budget_rate);
		return graph.resolve(source, sink, requested) == requested;
	};
	auto allocate_siblings = [&](ResourceIndex parent, ResourceAmount available)
	{
		std::vector<ResourceIndex> active;
		ResourceAmount reserved = 0;
		for (ResourceIndex i = 0; i < groups_.size(); ++i)
			if (definition_.groups[i].parent == parent && !groups_[i].cut)
			{
				active.push_back(i);
				reserved += groups_[i].budget_rate;
			}
		while (!active.empty() && (reserved > available || !feasible(active, reserved)))
		{
			auto p = static_cast<std::size_t>(random_next() % active.size());
			auto i = active[p];
			groups_[i].cut = true;
			groups_[i].sustainable_ticks = 0;
			reserved -= groups_[i].budget_rate;
			active.erase(active.begin() + p);
		}
		std::vector<ResourceIndex> eligible;
		for (ResourceIndex i = 0; i < groups_.size(); ++i)
			if (definition_.groups[i].parent == parent && groups_[i].cut)
			{
				auto &s = groups_[i];
				const auto &d = definition_.groups[i];
				auto trial = active;
				trial.push_back(i);
				if (reserved + s.budget_rate + d.restore_margin_rate <= available &&
				    feasible(trial, reserved + s.budget_rate))
				{
					if (s.sustainable_ticks < d.restore_ticks)
						++s.sustainable_ticks;
					if (s.sustainable_ticks >= d.restore_ticks)
						eligible.push_back(i);
				}
				else
					s.sustainable_ticks = 0;
			}
		while (!eligible.empty())
		{
			auto p = static_cast<std::size_t>(random_next() % eligible.size());
			auto i = eligible[p];
			auto &s = groups_[i];
			auto trial = active;
			trial.push_back(i);
			if (reserved + s.budget_rate + definition_.groups[i].restore_margin_rate <= available &&
			    feasible(trial, reserved + s.budget_rate))
			{
				s.cut = false;
				s.sustainable_ticks = 0;
				reserved += s.budget_rate;
				active.push_back(i);
			}
			eligible.erase(eligible.begin() + p);
		}
	};
	allocate_siblings(no_resource, supply);
	// Parent-first independent of authored array order.
	for (std::size_t depth = 0; depth < groups_.size(); ++depth)
		for (ResourceIndex i = 0; i < groups_.size(); ++i)
		{
			std::size_t actual = 0;
			auto parent = definition_.groups[i].parent;
			while (parent != no_resource)
			{
				++actual;
				parent = definition_.groups[parent].parent;
			}
			if (actual == depth)
				allocate_siblings(i, groups_[i].cut ? 0 : groups_[i].budget_rate);
		}
	for (ResourceIndex i = 0; i < groups_.size(); ++i)
		group_left_[i] = groups_[i].cut ? 0 : per_tick(groups_[i].budget_rate, groups_[i].tick_remainder);
	// Distribution adapts within the reserved budget to unpaid demand, then low reserves.
	// Stable identity breaks ties; this ordering never changes random overload eligibility.
	std::vector<ResourceIndex> recipients;
	for (ResourceIndex i = 0; i < stores_.size(); ++i)
		recipients.push_back(i);
	std::stable_sort(recipients.begin(), recipients.end(),
	                 [&](ResourceIndex a, ResourceIndex b)
	                 {
		                 const bool unpaid_a = demand_[a] > stores_[a].energy,
		                            unpaid_b = demand_[b] > stores_[b].energy;
		                 if (unpaid_a != unpaid_b)
			                 return unpaid_a;
		                 return stores_[a].energy * million / definition_.stores[a].capacity <
		                        stores_[b].energy * million / definition_.stores[b].capacity;
	                 });
	const auto group_base = stores_.size() * 2, source = group_base + groups_.size(), sink = source + 1;
	ResourceFlowGraph transfers(sink + 1);
	std::vector<ResourceFlowGraph::Handle> sources(stores_.size()), incoming(stores_.size());
	std::vector<ResourceFlowGraph::Handle> conduits(links_.size());
	for (ResourceIndex i = 0; i < stores_.size(); ++i)
	{
		const auto spare =
		    operational(i) ? std::min(discharge_left_[i], std::max<ResourceAmount>(0, stores_[i].energy - demand_[i]))
		                   : 0;
		sources[i] = transfers.add(source, i * 2 + 1, spare);
		if (operational(i))
			transfers.add(i * 2, i * 2 + 1, resource_limit * 4096);
	}
	for (ResourceIndex i = 0; i < links_.size(); ++i)
	{
		const auto &d = definition_.links[i];
		conduits[i] =
		    transfers.add(d.from * 2 + 1, d.to * 2, operational(d.from) && operational(d.to) ? link_left_[i] : 0);
	}
	for (ResourceIndex i = 0; i < groups_.size(); ++i)
	{
		const auto parent = definition_.groups[i].parent;
		transfers.add(group_base + i, parent == no_resource ? sink : group_base + parent, group_left_[i]);
	}
	for (const auto i : recipients)
	{
		const auto &d = definition_.stores[i];
		const auto capacity = operational(i) ? std::min(charge_left_[i], d.capacity - stores_[i].energy) : 0;
		incoming[i] = transfers.add(i * 2, d.group == no_resource ? sink : group_base + d.group, capacity);
		transfers.resolve(source, sink, resource_limit * 4096);
	}
	std::vector<ResourceAmount> resolved_links;
	for (ResourceIndex i = 0; i < links_.size(); ++i)
		resolved_links.push_back(transfers.used(conduits[i]));
	remove_resource_circulations(definition_, resolved_links);
	for (ResourceIndex i = 0; i < links_.size(); ++i)
		link_left_[i] -= resolved_links[i];
	for (ResourceIndex i = 0; i < stores_.size(); ++i)
	{
		auto sent = transfers.used(sources[i]), received = transfers.used(incoming[i]);
		const auto pass_through = std::min(sent, received);
		sent -= pass_through;
		received -= pass_through;
		auto &s = stores_[i];
		s.energy += received - sent;
		discharge_left_[i] -= sent;
		charge_left_[i] -= received;
		telemetry_[i].outgoing = sent;
		telemetry_[i].accepted_incoming = received;
		const auto discrete = operational(i) ? s.discrete_request : 0;
		const auto continuous = std::max<ResourceAmount>(0, demand_[i] - discrete);
		const auto paid = std::min({continuous, s.energy, discharge_left_[i]});
		debit_operation(i, paid, false);
		if (discrete && s.energy >= discrete && discharge_left_[i] >= discrete)
			debit_operation(i, discrete, true);
		telemetry_[i].limited = telemetry_[i].consumed < telemetry_[i].requested;
		const auto paid_continuous = telemetry_[i].consumed - (telemetry_[i].discrete_paid ? discrete : 0);
		const auto available_work = s.work_credit + paid_continuous * resource_work_unit;
		telemetry_[i].work_funded = std::clamp(available_work, ResourceAmount{0}, telemetry_[i].work_requested);
		s.work_credit = available_work - telemetry_[i].work_funded;
		telemetry_[i].limited |= telemetry_[i].work_funded < telemetry_[i].work_requested;
		demand_[i] = 0;
	}
	for (ResourceIndex i = 0; i < stores_.size(); ++i)
	{
		auto &s = stores_[i];
		const auto &d = definition_.stores[i];
		const auto incoming = telemetry_[i].accepted_incoming + telemetry_[i].generated;
		add_heat(i, scaled(incoming, d.charge_heat_ppm, s.charge_heat_remainder));
		add_consumption_heat(i, telemetry_[i].consumed,
		                     telemetry_[i].consumed + telemetry_[i].outgoing + telemetry_[i].lost);
		if (operational(i) &&
		    (telemetry_[i].work_funded > 0 || telemetry_[i].generated > 0 || telemetry_[i].discrete_paid))
		{
			const auto stress_rate =
			    (s.overdrive ? d.stress_rate : 0) + (s.thermal_override && s.thermal_stop ? d.override_stress_rate : 0);
			s.stress = std::min(d.stress_limit, s.stress + per_tick(stress_rate, s.stress_remainder));
		}
		const auto passive = std::min(s.heat, per_tick(d.passive_cooling, s.passive_remainder));
		s.heat -= passive;
		telemetry_[i].heat_removed += passive;
	}
	for (auto &source : coolant_sources_)
		source.last_heat_removed = source.last_consumed = 0;
	for (ResourceIndex i = 0; i < cooling_links_.size(); ++i)
	{
		auto &s = cooling_links_[i];
		const auto &d = definition_.cooling_links[i];
		auto &source = coolant_sources_[d.source];
		auto &store = stores_[d.store];
		const auto ratio = definition_.coolant_sources[d.source].coolant_per_heat_ppm;
		ResourceAmount output_permille = 1000;
		const auto pump = definition_.coolant_sources[d.source].power_store;
		if (pump != no_resource)
		{
			const auto &paid = telemetry_[pump];
			output_permille = funded_permille(paid.work_funded, paid.work_requested);
		}
		const auto capacity =
		    s.isolated ? 0 : divided(s.allocation_rate * output_permille, resource_tick_hz * 1000, s.tick_remainder);
		const auto affordable =
		    std::max<ResourceAmount>(0, source.remaining * million - source.consumption_remainder) / ratio;
		const auto removed = std::min({capacity, store.heat, affordable});
		const auto used = scaled(removed, ratio, source.consumption_remainder);
		source.remaining -= used;
		source.last_heat_removed += removed;
		source.last_consumed += used;
		store.heat -= removed;
		telemetry_[d.store].heat_removed += removed;
	}
	for (ResourceIndex i = 0; i < stores_.size(); ++i)
	{
		const auto &d = definition_.stores[i];
		if (d.stop_heat && stores_[i].heat >= d.stop_heat)
			stores_[i].thermal_stop = true;
		stores_[i].discrete_request = 0;
	}
}

ResourceForecast ResourceNetwork::forecast(ResourceIndex i) const
{
	const auto &s = stores_.at(i);
	const auto &d = definition_.stores.at(i);
	const auto &t = telemetry_.at(i);
	ResourceForecast f;
	f.observed_net_energy = t.accepted_incoming + t.generated - t.consumed - t.outgoing - t.lost;
	f.observed_net_heat = t.heat_added - t.heat_removed;
	if (!s.energy)
		f.depletion_ticks = 0;
	else if (f.observed_net_energy < 0)
		f.depletion_ticks = (s.energy - f.observed_net_energy - 1) / (-f.observed_net_energy);
	if (s.energy == d.capacity)
		f.full_ticks = 0;
	else if (f.observed_net_energy > 0)
		f.full_ticks = (d.capacity - s.energy + f.observed_net_energy - 1) / f.observed_net_energy;
	if (d.stop_heat && (s.heat >= d.stop_heat || f.observed_net_heat > 0))
		f.thermal_stop_ticks =
		    s.heat >= d.stop_heat ? 0 : (d.stop_heat - s.heat + f.observed_net_heat - 1) / f.observed_net_heat;
	if (s.thermal_stop)
	{
		if (s.heat <= d.recover_heat)
			f.thermal_recovery_ticks = 0;
		else if (f.observed_net_heat < 0)
			f.thermal_recovery_ticks = (s.heat - d.recover_heat - f.observed_net_heat - 1) / -f.observed_net_heat;
	}
	return f;
}
CoolantForecast ResourceNetwork::coolant_forecast(ResourceIndex source) const
{
	const auto &s = coolant_sources_.at(source);
	const auto &d = definition_.coolant_sources.at(source);
	CoolantForecast f;
	f.observed_heat_removed = s.last_heat_removed;
	f.observed_coolant_used = s.last_consumed;
	const auto available = std::max<ResourceAmount>(0, s.remaining * million - s.consumption_remainder);
	const auto consumption = s.last_heat_removed * d.coolant_per_heat_ppm;
	if (!available)
		f.depletion_ticks = 0;
	else if (consumption)
		f.depletion_ticks = (available + consumption - 1) / consumption;
	return f;
}

Json ResourceNetwork::save() const
{
	Json j = {{"version", 1},
	          {"definition", definition_.signature},
	          {"random", random_},
	          {"ticks", ticks_},
	          {"stores", Json::array()},
	          {"generators", Json::array()},
	          {"groups", Json::array()},
	          {"links", Json::array()},
	          {"coolant_sources", Json::array()},
	          {"cooling_links", Json::array()}};
	for (const auto &s : stores_)
		j["stores"].push_back({{"energy", s.energy},
		                       {"charge_limit", s.charge_limit},
		                       {"discharge_limit", s.discharge_limit},
		                       {"heat", s.heat},
		                       {"stress", s.stress},
		                       {"requested_rate", s.requested_rate},
		                       {"discrete_request", s.discrete_request},
		                       {"request_remainder", s.request_remainder},
		                       {"charge_remainder", s.charge_remainder},
		                       {"discharge_remainder", s.discharge_remainder},
		                       {"loss_remainder", s.loss_remainder},
		                       {"charge_heat_remainder", s.charge_heat_remainder},
		                       {"operation_heat_remainder", s.operation_heat_remainder},
		                       {"depletion_heat_remainder", s.depletion_heat_remainder},
		                       {"passive_remainder", s.passive_remainder},
		                       {"stress_remainder", s.stress_remainder},
		                       {"overdrive_heat_remainder", s.overdrive_heat_remainder},
		                       {"overdrive_request_remainder", s.overdrive_request_remainder},
		                       {"work_credit", s.work_credit},
		                       {"enabled", s.enabled},
		                       {"thermal_stop", s.thermal_stop},
		                       {"overdrive", s.overdrive},
		                       {"thermal_override", s.thermal_override}});
	for (const auto &s : generators_)
		j["generators"].push_back({{"commanded_rate", s.commanded_rate},
		                           {"actual_rate", s.actual_rate},
		                           {"response_remainder", s.response_remainder},
		                           {"generation_remainder", s.generation_remainder},
		                           {"heat_remainder", s.heat_remainder},
		                           {"enabled", s.enabled}});
	for (const auto &s : groups_)
		j["groups"].push_back({{"budget_rate", s.budget_rate},
		                       {"tick_remainder", s.tick_remainder},
		                       {"sustainable_ticks", s.sustainable_ticks},
		                       {"cut", s.cut}});
	for (const auto &s : links_)
		j["links"].push_back({{"tick_remainder", s.tick_remainder}, {"isolated", s.isolated}});
	for (const auto &s : coolant_sources_)
		j["coolant_sources"].push_back({{"remaining", s.remaining},
		                                {"consumption_remainder", s.consumption_remainder},
		                                {"last_heat_removed", s.last_heat_removed},
		                                {"last_consumed", s.last_consumed}});
	for (const auto &s : cooling_links_)
		j["cooling_links"].push_back(
		    {{"allocation_rate", s.allocation_rate}, {"tick_remainder", s.tick_remainder}, {"isolated", s.isolated}});
	// Last grants are observations, not spendable credit. Saving them preserves forecasts on recovery.
	j["telemetry"] = Json::array();
	for (const auto &t : telemetry_)
		j["telemetry"].push_back({{"requested", t.requested},
		                          {"accepted_incoming", t.accepted_incoming},
		                          {"outgoing", t.outgoing},
		                          {"generated", t.generated},
		                          {"consumed", t.consumed},
		                          {"work_requested", t.work_requested},
		                          {"work_funded", t.work_funded},
		                          {"lost", t.lost},
		                          {"heat_added", t.heat_added},
		                          {"heat_removed", t.heat_removed},
		                          {"discrete_paid", t.discrete_paid},
		                          {"limited", t.limited}});
	return j;
}

void ResourceNetwork::restore(const Json &j)
{
	if (!j.is_object() || j.value("version", 0) != 1 || j.value("definition", std::string()) != definition_.signature)
		throw std::runtime_error("resources.save: incompatible definition/version");
	ResourceNetwork next(definition_);
	auto sized = [&](const char *key, std::size_t size)
	{
		if (!j.contains(key) || !j.at(key).is_array() || j.at(key).size() != size)
			throw std::runtime_error(std::string("resources.save: invalid ") + key + " roster");
	};
	sized("stores", stores_.size());
	sized("generators", generators_.size());
	sized("groups", groups_.size());
	sized("links", links_.size());
	sized("coolant_sources", coolant_sources_.size());
	sized("cooling_links", cooling_links_.size());
	sized("telemetry", telemetry_.size());
	if (!j.contains("random") || !j.at("random").is_number_unsigned() || !j.at("random").get<std::uint64_t>() ||
	    !j.contains("ticks") || !j.at("ticks").is_number_unsigned())
		throw std::runtime_error("resources.save: invalid clock/random state");
	next.random_ = j.at("random").get<std::uint64_t>();
	next.ticks_ = j.at("ticks").get<std::uint64_t>();
	auto read = [&](const Json &record, const char *key, ResourceAmount maximum = resource_limit)
	{
		if (!record.contains(key))
			throw std::runtime_error(std::string("resources.save: missing ") + key);
		return amount(record, key, 0, maximum);
	};
	auto boolean = [&](const Json &record, const char *key)
	{
		if (!record.contains(key))
			throw std::runtime_error(std::string("resources.save: missing ") + key);
		return flag(record, key);
	};
	for (ResourceIndex i = 0; i < stores_.size(); ++i)
	{
		auto &s = next.stores_[i];
		const auto &d = definition_.stores[i];
		const auto &v = j.at("stores").at(i);
		s.energy = read(v, "energy", d.capacity);
		s.charge_limit = read(v, "charge_limit", d.charge_rate);
		s.discharge_limit = read(v, "discharge_limit", d.discharge_rate);
		s.heat = read(v, "heat");
		s.stress = read(v, "stress", d.stress_limit);
		s.requested_rate = read(v, "requested_rate");
		s.discrete_request = read(v, "discrete_request");
		s.request_remainder = read(v, "request_remainder", 19);
		s.charge_remainder = read(v, "charge_remainder", 19);
		s.discharge_remainder = read(v, "discharge_remainder", 19);
		s.passive_remainder = read(v, "passive_remainder", 19);
		s.stress_remainder = read(v, "stress_remainder", 19);
		s.overdrive_heat_remainder = read(v, "overdrive_heat_remainder", 999);
		s.overdrive_request_remainder = v.contains("overdrive_request_remainder")
		                                    ? read(v, "overdrive_request_remainder", resource_tick_hz * 1000 - 1)
		                                    : 0;
		if (v.contains("work_credit"))
		{
			const auto &credit = v.at("work_credit");
			if (!credit.is_number_integer() || credit.is_boolean() || credit < -2 * resource_work_unit ||
			    credit >= resource_work_unit)
				throw std::runtime_error("resources.save: invalid prepaid work credit");
			s.work_credit = credit.get<ResourceAmount>();
		}
		else
			s.work_credit = -s.request_remainder * 1000 - s.overdrive_request_remainder;
		// Old positive remainders represented work already performed but not yet
		// debited. Settle that liability before funding new work after migration.
		s.request_remainder = s.overdrive_request_remainder = 0;
		s.loss_remainder = read(v, "loss_remainder", million * resource_tick_hz - 1);
		s.charge_heat_remainder = read(v, "charge_heat_remainder", million - 1);
		s.operation_heat_remainder = read(v, "operation_heat_remainder", million - 1);
		s.depletion_heat_remainder = read(v, "depletion_heat_remainder", million - 1);
		s.enabled = boolean(v, "enabled");
		s.thermal_stop = boolean(v, "thermal_stop");
		s.overdrive = boolean(v, "overdrive");
		s.thermal_override = boolean(v, "thermal_override");
		if (s.thermal_override && !d.allows_override)
			throw std::runtime_error("resources.save: unsupported override");
		if (d.stop_heat && s.heat >= d.stop_heat && !s.thermal_stop)
			throw std::runtime_error("resources.save: missing thermal stop");
	}
	for (ResourceIndex i = 0; i < generators_.size(); ++i)
	{
		auto &s = next.generators_[i];
		const auto &d = definition_.generators[i];
		const auto &v = j.at("generators").at(i);
		s.commanded_rate = read(v, "commanded_rate", d.maximum_rate);
		s.actual_rate =
		    read(v, "actual_rate",
		         std::min(resource_limit, d.maximum_rate * definition_.stores[d.store].overdrive_permille / 1000));
		s.response_remainder = read(v, "response_remainder", 19);
		s.generation_remainder = read(v, "generation_remainder", 19);
		s.heat_remainder = read(v, "heat_remainder", million - 1);
		s.enabled = boolean(v, "enabled");
	}
	for (ResourceIndex i = 0; i < groups_.size(); ++i)
	{
		auto &s = next.groups_[i];
		const auto &v = j.at("groups").at(i);
		s.budget_rate = read(v, "budget_rate");
		s.tick_remainder = read(v, "tick_remainder", 19);
		s.sustainable_ticks =
		    static_cast<std::uint32_t>(read(v, "sustainable_ticks", definition_.groups[i].restore_ticks));
		s.cut = boolean(v, "cut");
	}
	for (ResourceIndex i = 0; i < links_.size(); ++i)
	{
		auto &s = next.links_[i];
		const auto &v = j.at("links").at(i);
		s.tick_remainder = read(v, "tick_remainder", 19);
		s.isolated = boolean(v, "isolated");
	}
	for (ResourceIndex i = 0; i < coolant_sources_.size(); ++i)
	{
		auto &s = next.coolant_sources_[i];
		const auto &v = j.at("coolant_sources").at(i);
		s.remaining = read(v, "remaining", definition_.coolant_sources[i].initial);
		s.consumption_remainder = read(v, "consumption_remainder", million - 1);
		s.last_heat_removed = v.contains("last_heat_removed") ? read(v, "last_heat_removed") : 0;
		s.last_consumed = v.contains("last_consumed") ? read(v, "last_consumed") : 0;
	}
	for (auto &s : next.cooling_links_)
		s.allocation_rate = 0;
	for (ResourceIndex i = 0; i < cooling_links_.size(); ++i)
	{
		const auto &v = j.at("cooling_links").at(i);
		next.set_cooling(i, read(v, "allocation_rate", definition_.cooling_links[i].capacity_rate),
		                 boolean(v, "isolated"));
		next.cooling_links_[i].tick_remainder = read(v, "tick_remainder", resource_tick_hz * 1000 - 1);
	}
	for (ResourceIndex i = 0; i < telemetry_.size(); ++i)
	{
		auto &t = next.telemetry_[i];
		const auto &v = j.at("telemetry").at(i);
		t.requested = read(v, "requested");
		t.accepted_incoming = read(v, "accepted_incoming");
		t.outgoing = read(v, "outgoing");
		t.generated = read(v, "generated");
		t.consumed = read(v, "consumed");
		t.work_requested = v.contains("work_requested") ? read(v, "work_requested", resource_limit * 10000) : 0;
		t.work_funded = v.contains("work_funded") ? read(v, "work_funded", t.work_requested) : 0;
		t.lost = read(v, "lost");
		t.heat_added = read(v, "heat_added");
		t.heat_removed = read(v, "heat_removed");
		t.discrete_paid = boolean(v, "discrete_paid");
		t.limited = boolean(v, "limited");
	}
	*this = std::move(next);
}
} // namespace phoenix
