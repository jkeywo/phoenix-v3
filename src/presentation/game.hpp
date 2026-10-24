#pragma once
namespace phoenix
{
class GameScreen
{
public:
	GameScreen();
	~GameScreen();
	struct Loading
	{
		bool active = false;
		std::size_t completed = 0, total = 0;
		std::string phase, title, error;
	};
	Loading preload(Application &app);
	void prepare(Application &app, Rml::ElementDocument &document);
	void refresh_status(Application &app);
	const SceneFrame &prepare_scene(const Application &app);
	void draw(const Application &app);
	void draw_viewscreen(Application &app, Rectangle region);

private:
	struct State;
	std::unique_ptr<State> state_;
};
} // namespace phoenix
