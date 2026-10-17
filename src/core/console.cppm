module;
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

export module phoenix.console;
export import phoenix.session.api;

export namespace phoenix
{
// One instance per routed Console. No renderer, device or authoritative world.
class ConsoleModel
{
public:
	void refresh(const std::shared_ptr<SessionPort> &session, std::uint64_t route = 1);
	void reset();
	bool act(const std::string &action);
	const std::optional<Observation> &observation() const
	{
		return observation_;
	}
	const Json *layout() const
	{
		return layout_.is_null() ? nullptr : &layout_;
	}
	std::int64_t requested_speed() const
	{
		return speed_;
	}
	std::int64_t requested_heading() const
	{
		return heading_;
	}
	const std::string &announcement() const
	{
		return announcement_;
	}
	std::string contacts_text() const;

private:
	std::weak_ptr<SessionPort> owner_;
	std::uint64_t generation_ = 0, route_ = 1;
	std::vector<std::pair<std::string, std::string>> packs_;
	std::optional<Observation> observation_;
	std::optional<std::uint64_t> pending_at_;
	std::int64_t speed_ = 0, heading_ = 0, maximum_ = 0;
	Json layout_;
	std::string announcement_;
};
} // namespace phoenix
