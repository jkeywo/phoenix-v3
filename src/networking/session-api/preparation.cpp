module;
#include <map>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <utility>
#include <vector>
module phoenix.session.api;
namespace phoenix
{
struct PresentationPreparation::State
{
	std::shared_ptr<const VerifiedPacks> source;
	std::shared_ptr<PresentationCache> cache;
	std::map<std::string, std::string> ships, keys, locations;
	std::map<std::string, std::shared_ptr<const PreparedPresentation>> results;
	std::unique_ptr<ContentDownload> download;
	std::string pending;
	std::uint64_t generation = 0;
};
PresentationPreparation::PresentationPreparation() : state_(std::make_unique<State>())
{
}
PresentationPreparation::~PresentationPreparation() = default;
void PresentationPreparation::begin(std::shared_ptr<const VerifiedPacks> source,
                                    std::map<std::string, std::string> ships, const NetworkOptions &options,
                                    std::uint64_t generation)
{
	std::map<std::string, std::string> keys;
	if (source)
		for (const auto &[ship, background] : ships)
			keys[ship] = ship_presentation_key(source->catalog(), ship, background);
	if (keys == state_->keys && state_->cache == options.presentations && (!keys.empty() || source == state_->source))
	{
		state_->generation = generation;
		return;
	}
	auto next = std::make_unique<State>();
	next->source = std::move(source);
	next->ships = std::move(ships);
	next->keys = std::move(keys);
	next->cache = options.presentations ? options.presentations : std::make_shared<PresentationCache>();
	next->locations = options.pack_urls;
	for (const auto &[hash, location] : options.pack_locations)
		next->locations[hash] = location;
	next->generation = generation;
	for (const auto &[ship, key] : next->keys)
		if (const auto value = next->cache->find(key))
			next->results[ship] = value;
	state_ = std::move(next);
}
void PresentationPreparation::poll()
{
	if (!state_->source)
		return;
	for (const auto &[ship, key] : state_->keys)
	{
		if (state_->results.contains(ship))
			continue;
		if (state_->pending != ship)
		{
			state_->pending = ship;
			state_->download =
			    open_content_artifact(ship_client_location(state_->source->catalog(), ship, state_->locations));
			if (state_->download)
				return;
		}
		auto bytes = state_->download ? state_->download->poll() : std::optional<Bytes>{Bytes{}};
		if (!bytes)
			return;
		state_->results[ship] = state_->cache->prepare(state_->source->catalog(), ship, state_->ships.at(ship), *bytes);
		state_->download.reset();
		state_->pending.clear();
		return;
	}
}
bool PresentationPreparation::ready() const
{
	return state_->keys.size() == state_->results.size();
}
std::shared_ptr<const PreparedPresentation> PresentationPreparation::get(const std::string &ship) const
{
	const auto found = state_->results.find(ship);
	return found == state_->results.end() ? nullptr : found->second;
}
} // namespace phoenix
