module;
#include <cstdint>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
export module phoenix.graphics.audio;
export import phoenix.content.catalog;

export import phoenix.graphics.audio_policy;
export import phoenix.graphics.scene;
export namespace phoenix
{
struct AudioLevels
{
	int master_volume = 50, music_volume = 50, siren_volume = 50, sfx_volume = 50;
};
// A permitted presentation snapshot. The caller keeps the catalogue alive through
// update; audio never queries a session or authoritative state of its own.
struct AudioPresentation
{
	AudioLevels levels;
	const void *session = nullptr;
	std::uint64_t generation = 0, tick = 0;
	const Catalog *catalog = nullptr;
	std::optional<BodyView> focus_body;
	Json alert = Json::object();
	bool playing = false, host = false, workshop = false, gm = false, recovering = false;
};
class ServerMusic
{
public:
	ServerMusic();
	~ServerMusic();
	ServerMusic(const ServerMusic &) = delete;
	ServerMusic &operator=(const ServerMusic &) = delete;
	void update(const AudioPresentation &, const SceneFrame &);
	void click(const AudioLevels &);

private:
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace phoenix
