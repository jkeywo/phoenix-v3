#pragma once
namespace phoenix
{
// The audio boundary consumes revisions, never replays historical state.
struct AlertAudioTransition
{
	const void *session = nullptr;
	std::uint64_t generation = 0, revision = 0;
	bool ready = false;
	inline bool observe(const void *current, std::uint64_t epoch, const Json &alert, bool enabled)
	{
		const bool same = session == current && generation == epoch;
		const auto next = enabled && !alert.empty() ? alert.at("revision").get<std::uint64_t>() : 0;
		const bool entry = same && ready && enabled && !alert.empty() && next != revision;
		session = current;
		generation = epoch;
		revision = next;
		ready = enabled && !alert.empty();
		return entry;
	}
};
inline std::string condition_music(const Json &alert)
{
	return alert.is_object() ? alert.value("music", std::string{}) : std::string{};
}
} // namespace phoenix
