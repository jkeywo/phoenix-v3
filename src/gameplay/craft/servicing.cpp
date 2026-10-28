module;
#include <cstdint>
#include <nlohmann/json.hpp>
#include <stdexcept>

module phoenix.gameplay.craft;

namespace phoenix
{
// The caller supplies its staged payment networks and the shared stock
// transaction. Native transfer still owns receiving/protection/discharge state.
std::int64_t stage_craft_service_energy(ResourceNetwork &source, ResourceNetwork &target, StockTransaction &stocks,
                                        ResourceIndex from, ResourceIndex to, std::int64_t offered, bool stage)
{
	const auto &resource = source.definition().stores.at(from).resource;
	if (!stage)
	{
		auto source_copy = source;
		auto target_copy = target;
		source_copy.stock(from) = stocks.view(source.stock(from));
		target_copy.stock(to) = stocks.view(target.stock(to));
		return source_copy.transfer_external(target_copy, from, to, offered);
	}
	auto source_stock = stocks.view(source.stock(from));
	auto target_stock = stocks.view(target.stock(to));
	source.stock(from).swap(source_stock);
	target.stock(to).swap(target_stock);
	std::int64_t received = 0;
	try
	{
		received = source.transfer_external(target, from, to, offered);
	}
	catch (...)
	{
		source.stock(from).swap(source_stock);
		target.stock(to).swap(target_stock);
		throw;
	}
	source.stock(from).swap(source_stock);
	target.stock(to).swap(target_stock);
	if (!stocks.transfer(source.stock(from), target.stock(to), resource, received))
		throw std::runtime_error("Servicing energy transaction lost its native transfer");
	return received;
}
} // namespace phoenix
