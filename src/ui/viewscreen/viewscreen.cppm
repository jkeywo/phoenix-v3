module;
#include <RmlUi/Core.h>
#include <raylib.h>
#include <cstddef>
#include <memory>
#include <string>
export module phoenix.ui.viewscreen;

export import phoenix.application;
export import phoenix.ui.interaction;
export import phoenix.graphics.scene;
export namespace phoenix
{
struct ViewscreenChrome
{
	float scale, corner;
	Rectangle condition_frame, condition_text;
};
ViewscreenChrome viewscreen_chrome(Rectangle area);
class GameScreen
{
public:
	GameScreen();
	~GameScreen();
	using Loading = LoadingProgress;
	Loading preload(Application &app);
	Loading preload_console(Application &app, ConsoleId route);
	bool loading_action(ConsoleId route, std::uint64_t request, bool retry);
	Loading console_preparation_failed(ConsoleId route, const std::string &error);
	bool scene_ready() const;
	void prepare(Application &app, Rml::ElementDocument &document);
	void refresh_status(Application &app);
	const SceneFrame &prepare_scene(Application &app);
	void draw(const Application &app);
	void draw_viewscreen(Application &app, Rectangle region);

private:
	Loading preload_step(Application &app);
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace phoenix
