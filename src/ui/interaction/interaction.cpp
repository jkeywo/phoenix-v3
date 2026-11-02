module;
#include <algorithm>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>
module phoenix.ui.interaction;

namespace phoenix
{
bool feature_action_enabled(const std::string &id)
{
	if (!PHOENIX_ENABLE_WORKSHOP && (id == "edit" || id == "menu.workshop"))
		return false;
	if (!PHOENIX_ENABLE_GM &&
	    (id == "menu.join-gm" || id == "menu.host-gm" || id == "game.local-gm" || id == "game.connect-gm" ||
	     id == "game.qr-gm" || id.starts_with("menu.inspector-screen/") || id.starts_with("network.gm/")))
		return false;
	if (!PHOENIX_ENABLE_MULTISHIP && id == "menu.join-ship")
		return false;
	if (!PHOENIX_ENABLE_MODS && (id == "menu.packs" || id == "menu.import-pack" || id.starts_with("menu.pack-") ||
	                             id == "menu.apply-packs" || id == "game.import" || id == "game.clear"))
		return false;
	return true;
}

std::uint64_t SurfaceLoad::begin(std::uint64_t target, const std::string &identity)
{
	if (identity_ == identity && progress_.target == target && progress_.request)
		return progress_.request;
	identity_ = identity;
	const auto ticket = progress_.request + 1;
	progress_ = {};
	progress_.active = true;
	progress_.target = target;
	progress_.request = ticket;
	return ticket;
}
bool SurfaceLoad::complete(std::uint64_t request)
{
	if (request != progress_.request || !progress_.active || progress_.state != LoadingProgress::State::preparing)
		return false;
	ready_identity_ = identity_;
	progress_.active = false;
	progress_.state = LoadingProgress::State::ready;
	return true;
}
bool SurfaceLoad::fail(std::uint64_t request, const std::string &error)
{
	if (request != progress_.request || !progress_.active || progress_.state != LoadingProgress::State::preparing)
		return false;
	progress_.state = LoadingProgress::State::failed;
	progress_.error = error;
	return true;
}
bool SurfaceLoad::cancel(std::uint64_t request)
{
	if (request != progress_.request || !progress_.active || progress_.state != LoadingProgress::State::preparing)
		return false;
	progress_.state = LoadingProgress::State::cancelled;
	progress_.phase = "Loading cancelled";
	progress_.error.clear();
	return true;
}
void SurfaceLoad::retry()
{
	++progress_.request;
	progress_.active = true;
	progress_.state = LoadingProgress::State::preparing;
	progress_.error.clear();
}
void SurfaceLoad::advance(std::size_t completed, std::size_t total, const std::string &phase)
{
	progress_.completed = completed;
	progress_.total = total;
	progress_.phase = phase;
}
bool same_semantic_controls(const std::vector<SemanticControl> &a, const std::vector<SemanticControl> &b)
{
	if (a.size() != b.size())
		return false;
	const auto fields = [](const SemanticControl &c)
	{
		return std::tie(c.id, c.label, c.placement.x, c.placement.y, c.placement.width, c.placement.height,
		                c.placement.focused, c.placement.enabled, c.editable, c.value, c.percentage, c.read_only, c.tab,
		                c.selected, c.toggle, c.pressed, c.numeric, c.minimum, c.maximum, c.step);
	};
	return std::equal(a.begin(), a.end(), b.begin(),
	                  [&](const auto &x, const auto &y)
	                  {
		                  return fields(x) == fields(y);
	                  });
}
} // namespace phoenix
